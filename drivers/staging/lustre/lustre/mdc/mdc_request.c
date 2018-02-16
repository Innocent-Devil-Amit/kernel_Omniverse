/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_MDC

# include <linux/module.h>
# include <linux/pagemap.h>
# include <linux/miscdevice.h>
# include <linux/init.h>
# include <linux/utsname.h>

#include "../include/lustre_acl.h"
#include "../include/obd_class.h"
#include "../include/lustre_fid.h"
#include "../include/lprocfs_status.h"
#include "../include/lustre_param.h"
#include "../include/lustre_log.h"

#include "mdc_internal.h"

#define REQUEST_MINOR 244

struct mdc_renew_capa_args {
	struct obd_capa	*ra_oc;
	renew_capa_cb_t	 ra_cb;
};

static int mdc_cleanup(struct obd_device *obd);

int mdc_unpack_capa(struct obd_export *exp, struct ptlrpc_request *req,
		    const struct req_msg_field *field, struct obd_capa **oc)
{
	struct lustre_capa *capa;
	struct obd_capa *c;

	/* swabbed already in mdc_enqueue */
	capa = req_capsule_server_get(&req->rq_pill, field);
	if (capa == NULL)
		return -EPROTO;

	c = alloc_capa(CAPA_SITE_CLIENT);
	if (IS_ERR(c)) {
		CDEBUG(D_INFO, "alloc capa failed!\n");
		return PTR_ERR(c);
	} else {
		c->c_capa = *capa;
		*oc = c;
		return 0;
	}
}

static inline int mdc_queue_wait(struct ptlrpc_request *req)
{
	struct client_obd *cli = &req->rq_import->imp_obd->u.cli;
	int rc;

	/* mdc_enter_request() ensures that this client has no more
	 * than cl_max_rpcs_in_flight RPCs simultaneously inf light
	 * against an MDT. */
	rc = mdc_enter_request(cli);
	if (rc != 0)
		return rc;

	rc = ptlrpc_queue_wait(req);
	mdc_exit_request(cli);

	return rc;
}

/* Helper that implements most of mdc_getstatus and signal_completed_replay. */
/* XXX this should become mdc_get_info("key"), sending MDS_GET_INFO RPC */
static int send_getstatus(struct obd_import *imp, struct lu_fid *rootfid,
			  struct obd_capa **pc, int level, int msg_flags)
{
	struct ptlrpc_request *req;
	struct mdt_body       *body;
	int		    rc;

	req = ptlrpc_request_alloc_pack(imp, &RQF_MDS_GETSTATUS,
					LUSTRE_MDS_VERSION, MDS_GETSTATUS);
	if (req == NULL)
		return -ENOMEM;

	mdc_pack_body(req, NULL, NULL, 0, 0, -1, 0);
	lustre_msg_add_flags(req->rq_reqmsg, msg_flags);
	req->rq_send_state = level;

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		goto out;

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	if (body == NULL) {
		rc = -EPROTO;
		goto out;
	}

	if (body->valid & OBD_MD_FLMDSCAPA) {
		rc = mdc_unpack_capa(NULL, req, &RMF_CAPA1, pc);
		if (rc)
			goto out;
	}

	*rootfid = body->fid1;
	CDEBUG(D_NET,
	       "root fid="DFID", last_committed=%llu\n",
	       PFID(rootfid),
	       lustre_msg_get_last_committed(req->rq_repmsg));
out:
	ptlrpc_req_finished(req);
	return rc;
}

/* This should be mdc_get_info("rootfid") */
int mdc_getstatus(struct obd_export *exp, struct lu_fid *rootfid,
		  struct obd_capa **pc)
{
	return send_getstatus(class_exp2cliimp(exp), rootfid, pc,
			      LUSTRE_IMP_FULL, 0);
}

/*
 * This function now is known to always saying that it will receive 4 buffers
 * from server. Even for cases when acl_size and md_size is zero, RPC header
 * will contain 4 fields and RPC itself will contain zero size fields. This is
 * because mdt_getattr*() _always_ returns 4 fields, but if acl is not needed
 * and thus zero, it shrinks it, making zero size. The same story about
 * md_size. And this is course of problem when client waits for smaller number
 * of fields. This issue will be fixed later when client gets aware of RPC
 * layouts.  --umka
 */
static int mdc_getattr_common(struct obd_export *exp,
			      struct ptlrpc_request *req)
{
	struct req_capsule *pill = &req->rq_pill;
	struct mdt_body    *body;
	void	       *eadata;
	int		 rc;

	/* Request message already built. */
	rc = ptlrpc_queue_wait(req);
	if (rc != 0)
		return rc;

	/* sanity check for the reply */
	body = req_capsule_server_get(pill, &RMF_MDT_BODY);
	if (body == NULL)
		return -EPROTO;

	CDEBUG(D_NET, "mode: %o\n", body->mode);

	if (body->eadatasize != 0) {
		mdc_update_max_ea_from_body(exp, body);

		eadata = req_capsule_server_sized_get(pill, &RMF_MDT_MD,
						      body->eadatasize);
		if (eadata == NULL)
			return -EPROTO;
	}

	if (body->valid & OBD_MD_FLRMTPERM) {
		struct mdt_remote_perm *perm;

		LASSERT(client_is_remote(exp));
		perm = req_capsule_server_swab_get(pill, &RMF_ACL,
						lustre_swab_mdt_remote_perm);
		if (perm == NULL)
			return -EPROTO;
	}

	if (body->valid & OBD_MD_FLMDSCAPA) {
		struct lustre_capa *capa;

		capa = req_capsule_server_get(pill, &RMF_CAPA1);
		if (capa == NULL)
			return -EPROTO;
	}

	return 0;
}

int mdc_getattr(struct obd_export *exp, struct md_op_data *op_data,
		struct ptlrpc_request **request)
{
	struct ptlrpc_request *req;
	int		    rc;

	/* Single MDS without an LMV case */
	if (op_data->op_flags & MF_GET_MDT_IDX) {
		op_data->op_mds = 0;
		return 0;
	}
	*request = NULL;
	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_MDS_GETATTR);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_GETATTR);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_pack_body(req, &op_data->op_fid1, op_data->op_capa1,
		      op_data->op_valid, op_data->op_mode, -1, 0);

	req_capsule_set_size(&req->rq_pill, &RMF_MDT_MD, RCL_SERVER,
			     op_data->op_mode);
	if (op_data->op_valid & OBD_MD_FLRMTPERM) {
		LASSERT(client_is_remote(exp));
		req_capsule_set_size(&req->rq_pill, &RMF_ACL, RCL_SERVER,
				     sizeof(struct mdt_remote_perm));
	}
	ptlrpc_request_set_replen(req);

	rc = mdc_getattr_common(exp, req);
	if (rc)
		ptlrpc_req_finished(req);
	else
		*request = req;
	return rc;
}

int mdc_getattr_name(struct obd_export *exp, struct md_op_data *op_data,
		     struct ptlrpc_request **request)
{
	struct ptlrpc_request *req;
	int		    rc;

	*request = NULL;
	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_MDS_GETATTR_NAME);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);
	req_capsule_set_size(&req->rq_pill, &RMF_NAME, RCL_CLIENT,
			     op_data->op_namelen + 1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_GETATTR_NAME);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_pack_body(req, &op_data->op_fid1, op_data->op_capa1,
		      op_data->op_valid, op_data->op_mode,
		      op_data->op_suppgids[0], 0);

	if (op_data->op_name) {
		char *name = req_capsule_client_get(&req->rq_pill, &RMF_NAME);

		LASSERT(strnlen(op_data->op_name, op_data->op_namelen) ==
				op_data->op_namelen);
		memcpy(name, op_data->op_name, op_data->op_namelen);
	}

	req_capsule_set_size(&req->rq_pill, &RMF_MDT_MD, RCL_SERVER,
			     op_data->op_mode);
	ptlrpc_request_set_replen(req);

	rc = mdc_getattr_common(exp, req);
	if (rc)
		ptlrpc_req_finished(req);
	else
		*request = req;
	return rc;
}

static int mdc_is_subdir(struct obd_export *exp,
			 const struct lu_fid *pfid,
			 const struct lu_fid *cfid,
			 struct ptlrpc_request **request)
{
	struct ptlrpc_request  *req;
	int		     rc;

	*request = NULL;
	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp),
					&RQF_MDS_IS_SUBDIR, LUSTRE_MDS_VERSION,
					MDS_IS_SUBDIR);
	if (req == NULL)
		return -ENOMEM;

	mdc_is_subdir_pack(req, pfid, cfid, 0);
	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc && rc != -EREMOTE)
		ptlrpc_req_finished(req);
	else
		*request = req;
	return rc;
}

static int mdc_xattr_common(struct obd_export *exp,
			    const struct req_format *fmt,
			    const struct lu_fid *fid,
			    struct obd_capa *oc, int opcode, u64 valid,
			    const char *xattr_name, const char *input,
			    int input_size, int output_size, int flags,
			    __u32 suppgid, struct ptlrpc_request **request)
{
	struct ptlrpc_request *req;
	int   xattr_namelen = 0;
	char *tmp;
	int   rc;

	*request = NULL;
	req = ptlrpc_request_alloc(class_exp2cliimp(exp), fmt);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, oc);
	if (xattr_name) {
		xattr_namelen = strlen(xattr_name) + 1;
		req_capsule_set_size(&req->rq_pill, &RMF_NAME, RCL_CLIENT,
				     xattr_namelen);
	}
	if (input_size) {
		LASSERT(input);
		req_capsule_set_size(&req->rq_pill, &RMF_EADATA, RCL_CLIENT,
				     input_size);
	}

	/* Flush local XATTR locks to get rid of a possible cancel RPC */
	if (opcode == MDS_REINT && fid_is_sane(fid) &&
	    exp->exp_connect_data.ocd_ibits_known & MDS_INODELOCK_XATTR) {
		LIST_HEAD(cancels);
		int count;

		/* Without that packing would fail */
		if (input_size == 0)
			req_capsule_set_size(&req->rq_pill, &RMF_EADATA,
					     RCL_CLIENT, 0);

		count = mdc_resource_get_unused(exp, fid,
						&cancels, LCK_EX,
						MDS_INODELOCK_XATTR);

		rc = mdc_prep_elc_req(exp, req, MDS_REINT, &cancels, count);
		if (rc) {
			ptlrpc_request_free(req);
			return rc;
		}
	} else {
		rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, opcode);
		if (rc) {
			ptlrpc_request_free(req);
			return rc;
		}
	}

	if (opcode == MDS_REINT) {
		struct mdt_rec_setxattr *rec;

		CLASSERT(sizeof(struct mdt_rec_setxattr) ==
			 sizeof(struct mdt_rec_reint));
		rec = req_capsule_client_get(&req->rq_pill, &RMF_REC_REINT);
		rec->sx_opcode = REINT_SETXATTR;
		rec->sx_fsuid  = from_kuid(&init_user_ns, current_fsuid());
		rec->sx_fsgid  = from_kgid(&init_user_ns, current_fsgid());
		rec->sx_cap    = cfs_curproc_cap_pack();
		rec->sx_suppgid1 = suppgid;
		rec->sx_suppgid2 = -1;
		rec->sx_fid    = *fid;
		rec->sx_valid  = valid | OBD_MD_FLCTIME;
		rec->sx_time   = get_seconds();
		rec->sx_size   = output_size;
		rec->sx_flags  = flags;

		mdc_pack_capa(req, &RMF_CAPA1, oc);
	} else {
		mdc_pack_body(req, fid, oc, valid, output_size, suppgid, flags);
	}

	if (xattr_name) {
		tmp = req_capsule_client_get(&req->rq_pill, &RMF_NAME);
		memcpy(tmp, xattr_name, xattr_namelen);
	}
	if (input_size) {
		tmp = req_capsule_client_get(&req->rq_pill, &RMF_EADATA);
		memcpy(tmp, input, input_size);
	}

	if (req_capsule_has_field(&req->rq_pill, &RMF_EADATA, RCL_SERVER))
		req_capsule_set_size(&req->rq_pill, &RMF_EADATA,
				     RCL_SERVER, output_size);
	ptlrpc_request_set_replen(req);

	/* make rpc */
	if (opcode == MDS_REINT)
		mdc_get_rpc_lock(exp->exp_obd->u.cli.cl_rpc_lock, NULL);

	rc = ptlrpc_queue_wait(req);

	if (opcode == MDS_REINT)
		mdc_put_rpc_lock(exp->exp_obd->u.cli.cl_rpc_lock, NULL);

	if (rc)
		ptlrpc_req_finished(req);
	else
		*request = req;
	return rc;
}

int mdc_setxattr(struct obd_export *exp, const struct lu_fid *fid,
		 struct obd_capa *oc, u64 valid, const char *xattr_name,
		 const char *input, int input_size, int output_size,
		 int flags, __u32 suppgid, struct ptlrpc_request **request)
{
	return mdc_xattr_common(exp, &RQF_MDS_REINT_SETXATTR,
				fid, oc, MDS_REINT, valid, xattr_name,
				input, input_size, output_size, flags,
				suppgid, request);
}

int mdc_getxattr(struct obd_export *exp, const struct lu_fid *fid,
		 struct obd_capa *oc, u64 valid, const char *xattr_name,
		 const char *input, int input_size, int output_size,
		 int flags, struct ptlrpc_request **request)
{
	return mdc_xattr_common(exp, &RQF_MDS_GETXATTR,
				fid, oc, MDS_GETXATTR, valid, xattr_name,
				input, input_size, output_size, flags,
				-1, request);
}

#ifdef CONFIG_FS_POSIX_ACL
static int mdc_unpack_acl(struct ptlrpc_request *req, struct lustre_md *md)
{
	struct req_capsule     *pill = &req->rq_pill;
	struct mdt_body	*body = md->body;
	struct posix_acl       *acl;
	void		   *buf;
	int		     rc;

	if (!body->aclsize)
		return 0;

	buf = req_capsule_server_sized_get(pill, &RMF_ACL, body->aclsize);

	if (!buf)
		return -EPROTO;

	acl = posix_acl_from_xattr(&init_user_ns, buf, body->aclsize);
	if (IS_ERR(acl)) {
		rc = PTR_ERR(acl);
		CERROR("convert xattr to acl: %d\n", rc);
		return rc;
	}

	rc = posix_acl_valid(acl);
	if (rc) {
		CERROR("validate acl: %d\n", rc);
		posix_acl_release(acl);
		return rc;
	}

	md->posix_acl = acl;
	return 0;
}
#else
#define mdc_unpack_acl(req, md) 0
#endif

int mdc_get_lustre_md(struct obd_export *exp, struct ptlrpc_request *req,
		      struct obd_export *dt_exp, struct obd_export *md_exp,
		      struct lustre_md *md)
{
	struct req_capsule *pill = &req->rq_pill;
	int rc;

	LASSERT(md);
	memset(md, 0, sizeof(*md));

	md->body = req_capsule_server_get(pill, &RMF_MDT_BODY);
	LASSERT(md->body != NULL);

	if (md->body->valid & OBD_MD_FLEASIZE) {
		int lmmsize;
		struct lov_mds_md *lmm;

		if (!S_ISREG(md->body->mode)) {
			CDEBUG(D_INFO,
			       "OBD_MD_FLEASIZE set, should be a regular file, but is not\n");
			rc = -EPROTO;
			goto out;
		}

		if (md->body->eadatasize == 0) {
			CDEBUG(D_INFO,
			       "OBD_MD_FLEASIZE set, but eadatasize 0\n");
			rc = -EPROTO;
			goto out;
		}
		lmmsize = md->body->eadatasize;
		lmm = req_capsule_server_sized_get(pill, &RMF_MDT_MD, lmmsize);
		if (!lmm) {
			rc = -EPROTO;
			goto out;
		}

		rc = obd_unpackmd(dt_exp, &md->lsm, lmm, lmmsize);
		if (rc < 0)
			goto out;

		if (rc < sizeof(*md->lsm)) {
			CDEBUG(D_INFO,
			       "lsm size too small: rc < sizeof (*md->lsm) (%d < %d)\n",
			       rc, (int)sizeof(*md->lsm));
			rc = -EPROTO;
			goto out;
		}

	} else if (md->body->valid & OBD_MD_FLDIREA) {
		int lmvsize;
		struct lov_mds_md *lmv;

		if (!S_ISDIR(md->body->mode)) {
			CDEBUG(D_INFO,
			       "OBD_MD_FLDIREA set, should be a directory, but is not\n");
			rc = -EPROTO;
			goto out;
		}

		if (md->body->eadatasize == 0) {
			CDEBUG(D_INFO,
			       "OBD_MD_FLDIREA is set, but eadatasize 0\n");
			return -EPROTO;
		}
		if (md->body->valid & OBD_MD_MEA) {
			lmvsize = md->body->eadatasize;
			lmv = req_capsule_server_sized_get(pill, &RMF_MDT_MD,
							   lmvsize);
			if (!lmv) {
				rc = -EPROTO;
				goto out;
			}

			rc = obd_unpackmd(md_exp, (void *)&md->mea, lmv,
					  lmvsize);
			if (rc < 0)
				goto out;

			if (rc < sizeof(*md->mea)) {
				CDEBUG(D_INFO,
				       "size too small: rc < sizeof(*md->mea) (%d < %d)\n",
					rc, (int)sizeof(*md->mea));
				rc = -EPROTO;
				goto out;
			}
		}
	}
	rc = 0;

	if (md->body->valid & OBD_MD_FLRMTPERM) {
		/* remote permission */
		LASSERT(client_is_remote(exp));
		md->remote_perm = req_capsule_server_swab_get(pill, &RMF_ACL,
						lustre_swab_mdt_remote_perm);
		if (!md->remote_perm) {
			rc = -EPROTO;
			goto out;
		}
	} else if (md->body->valid & OBD_MD_FLACL) {
		/* for ACL, it's possible that FLACL is set but aclsize is zero.
		 * only when aclsize != 0 there's an actual segment for ACL
		 * in reply buffer.
		 */
		if (md->body->aclsize) {
			rc = mdc_unpack_acl(req, md);
			if (rc)
				goto out;
#ifdef CONFIG_FS_POSIX_ACL
		} else {
			md->posix_acl = NULL;
#endif
		}
	}
	if (md->body->valid & OBD_MD_FLMDSCAPA) {
		struct obd_capa *oc = NULL;

		rc = mdc_unpack_capa(NULL, req, &RMF_CAPA1, &oc);
		if (rc)
			goto out;
		md->mds_capa = oc;
	}

	if (md->body->valid & OBD_MD_FLOSSCAPA) {
		struct obd_capa *oc = NULL;

		rc = mdc_unpack_capa(NULL, req, &RMF_CAPA2, &oc);
		if (rc)
			goto out;
		md->oss_capa = oc;
	}

out:
	if (rc) {
		if (md->oss_capa) {
			capa_put(md->oss_capa);
			md->oss_capa = NULL;
		}
		if (md->mds_capa) {
			capa_put(md->mds_capa);
			md->mds_capa = NULL;
		}
#ifdef CONFIG_FS_POSIX_ACL
		posix_acl_release(md->posix_acl);
#endif
		if (md->lsm)
			obd_free_memmd(dt_exp, &md->lsm);
	}
	return rc;
}

int mdc_free_lustre_md(struct obd_export *exp, struct lustre_md *md)
{
	return 0;
}

/**
 * Handles both OPEN and SETATTR RPCs for OPEN-CLOSE and SETATTR-DONE_WRITING
 * RPC chains.
 */
void mdc_replay_open(struct ptlrpc_request *req)
{
	struct md_open_data *mod = req->rq_cb_data;
	struct ptlrpc_request *close_req;
	struct obd_client_handle *och;
	struct lustre_handle old;
	struct mdt_body *body;

	if (mod == NULL) {
		DEBUG_REQ(D_ERROR, req,
			  "Can't properly replay without open data.");
		return;
	}

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	LASSERT(body != NULL);

	och = mod->mod_och;
	if (och != NULL) {
		struct lustre_handle *file_fh;

		LASSERT(och->och_magic == OBD_CLIENT_HANDLE_MAGIC);

		file_fh = &och->och_fh;
		CDEBUG(D_HA, "updating handle from %#llx to %#llx\n",
		       file_fh->cookie, body->handle.cookie);
		old = *file_fh;
		*file_fh = body->handle;
	}
	close_req = mod->mod_close_req;
	if (close_req != NULL) {
		__u32 opc = lustre_msg_get_opc(close_req->rq_reqmsg);
		struct mdt_ioepoch *epoch;

		LASSERT(opc == MDS_CLOSE || opc == MDS_DONE_WRITING);
		epoch = req_capsule_client_get(&close_req->rq_pill,
					       &RMF_MDT_EPOCH);
		LASSERT(epoch);

		if (och != NULL)
			LASSERT(!memcmp(&old, &epoch->handle, sizeof(old)));
		DEBUG_REQ(D_HA, close_req, "updating close body with new fh");
		epoch->handle = body->handle;
	}
}

void mdc_commit_open(struct ptlrpc_request *req)
{
	struct md_open_data *mod = req->rq_cb_data;

	if (mod == NULL)
		return;

	/**
	 * No need to touch md_open_data::mod_och, it holds a reference on
	 * \var mod and will zero references to each other, \var mod will be
	 * freed after that when md_open_data::mod_och will put the reference.
	 */

	/**
	 * Do not let open request to disappear as it still may be needed
	 * for close rpc to happen (it may happen on evict only, otherwise
	 * ptlrpc_request::rq_replay does not let mdc_commit_open() to be
	 * called), just mark this rpc as committed to distinguish these 2
	 * cases, see mdc_close() for details. The open request reference will
	 * be put along with freeing \var mod.
	 */
	ptlrpc_request_addref(req);
	spin_lock(&req->rq_lock);
	req->rq_committed = 1;
	spin_unlock(&req->rq_lock);
	req->rq_cb_data = NULL;
	obd_mod_put(mod);
}

int mdc_set_open_replay_data(struct obd_export *exp,
			     struct obd_client_handle *och,
			     struct lookup_intent *it)
{
	struct md_open_data   *mod;
	struct mdt_rec_create *rec;
	struct mdt_body       *body;
	struct ptlrpc_request *open_req = it->d.lustre.it_data;
	struct obd_import     *imp = open_req->rq_import;

	if (!open_req->rq_replay)
		return 0;

	rec = req_capsule_client_get(&open_req->rq_pill, &RMF_REC_REINT);
	body = req_capsule_server_get(&open_req->rq_pill, &RMF_MDT_BODY);
	LASSERT(rec != NULL);
	/* Incoming message in my byte order (it's been swabbed). */
	/* Outgoing messages always in my byte order. */
	LASSERT(body != NULL);

	/* Only if the import is replayable, we set replay_open data */
	if (och && imp->imp_replayable) {
		mod = obd_mod_alloc();
		if (mod == NULL) {
			DEBUG_REQ(D_ERROR, open_req,
				  "Can't allocate md_open_data");
			return 0;
		}

		/**
		 * Take a reference on \var mod, to be freed on mdc_close().
		 * It protects \var mod from being freed on eviction (commit
		 * callback is called despite rq_replay flag).
		 * Another reference for \var och.
		 */
		obd_mod_get(mod);
		obd_mod_get(mod);

		spin_lock(&open_req->rq_lock);
		och->och_mod = mod;
		mod->mod_och = och;
		mod->mod_is_create = it_disposition(it, DISP_OPEN_CREATE) ||
				     it_disposition(it, DISP_OPEN_STRIPE);
		mod->mod_open_req = open_req;
		open_req->rq_cb_data = mod;
		open_req->rq_commit_cb = mdc_commit_open;
		spin_unlock(&open_req->rq_lock);
	}

	rec->cr_fid2 = body->fid1;
	rec->cr_ioepoch = body->ioepoch;
	rec->cr_old_handle.cookie = body->handle.cookie;
	open_req->rq_replay_cb = mdc_replay_open;
	if (!fid_is_sane(&body->fid1)) {
		DEBUG_REQ(D_ERROR, open_req,
			  "Saving replay request with insane fid");
		LBUG();
	}

	DEBUG_REQ(D_RPCTRACE, open_req, "Set up open replay data");
	return 0;
}

static void mdc_free_open(struct md_open_data *mod)
{
	int committed = 0;

	if (mod->mod_is_create == 0 &&
	    imp_connect_disp_stripe(mod->mod_open_req->rq_import))
		committed = 1;

	LASSERT(mod->mod_open_req->rq_replay == 0);

	DEBUG_REQ(D_RPCTRACE, mod->mod_open_req, "free open request\n");

	ptlrpc_request_committed(mod->mod_open_req, committed);
	if (mod->mod_close_req)
		ptlrpc_request_committed(mod->mod_close_req, committed);
}

int mdc_clear_open_replay_data(struct obd_export *exp,
			       struct obd_client_handle *och)
{
	struct md_open_data *mod = och->och_mod;

	/**
	 * It is possible to not have \var mod in a case of eviction between
	 * lookup and ll_file_open().
	 **/
	if (mod == NULL)
		return 0;

	LASSERT(mod != LP_POISON);
	LASSERT(mod->mod_open_req != NULL);
	mdc_free_open(mod);

	mod->mod_och = NULL;
	och->och_mod = NULL;
	obd_mod_put(mod);

	return 0;
}

/* Prepares the request for the replay by the given reply */
static void mdc_close_handle_reply(struct ptlrpc_request *req,
				   struct md_op_data *op_data, int rc) {
	struct mdt_body  *repbody;
	struct mdt_ioepoch *epoch;

	if (req && rc == -EAGAIN) {
		repbody = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
		epoch = req_capsule_client_get(&req->rq_pill, &RMF_MDT_EPOCH);

		epoch->flags |= MF_SOM_AU;
		if (repbody->valid & OBD_MD_FLGETATTRLOCK)
			op_data->op_flags |= MF_GETATTR_LOCK;
	}
}

int mdc_close(struct obd_export *exp, struct md_op_data *op_data,
	      struct md_open_data *mod, struct ptlrpc_request **request)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct ptlrpc_request *req;
	struct req_format     *req_fmt;
	int                    rc;
	int		       saved_rc = 0;


	req_fmt = &RQF_MDS_CLOSE;
	if (op_data->op_bias & MDS_HSM_RELEASE) {
		req_fmt = &RQF_MDS_RELEASE_CLOSE;

		/* allocate a FID for volatile file */
		rc = mdc_fid_alloc(exp, &op_data->op_fid2, op_data);
		if (rc < 0) {
			CERROR("%s: "DFID" failed to allocate FID: %d\n",
			       obd->obd_name, PFID(&op_data->op_fid1), rc);
			/* save the errcode and proceed to close */
			saved_rc = rc;
		}
	}

	*request = NULL;
	req = ptlrpc_request_alloc(class_exp2cliimp(exp), req_fmt);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_CLOSE);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	/* To avoid a livelock (bug 7034), we need to send CLOSE RPCs to a
	 * portal whose threads are not taking any DLM locks and are therefore
	 * always progressing */
	req->rq_request_portal = MDS_READPAGE_PORTAL;
	ptlrpc_at_set_req_timeout(req);

	/* Ensure that this close's handle is fixed up during replay. */
	if (likely(mod != NULL)) {
		LASSERTF(mod->mod_open_req != NULL &&
			 mod->mod_open_req->rq_type != LI_POISON,
			 "POISONED open %p!\n", mod->mod_open_req);

		mod->mod_close_req = req;

		DEBUG_REQ(D_HA, mod->mod_open_req, "matched open");
		/* We no longer want to preserve this open for replay even
		 * though the open was committed. b=3632, b=3633 */
		spin_lock(&mod->mod_open_req->rq_lock);
		mod->mod_open_req->rq_replay = 0;
		spin_unlock(&mod->mod_open_req->rq_lock);
	} else {
		 CDEBUG(D_HA,
			"couldn't find open req; expecting close error\n");
	}

	mdc_close_pack(req, op_data);

	req_capsule_set_size(&req->rq_pill, &RMF_MDT_MD, RCL_SERVER,
			     obd->u.cli.cl_default_mds_easize);
	req_capsule_set_size(&req->rq_pill, &RMF_LOGCOOKIES, RCL_SERVER,
			     obd->u.cli.cl_default_mds_cookiesize);

	ptlrpc_request_set_replen(req);

	mdc_get_rpc_lock(obd->u.cli.cl_close_lock, NULL);
	rc = ptlrpc_queue_wait(req);
	mdc_put_rpc_lock(obd->u.cli.cl_close_lock, NULL);

	if (req->rq_repmsg == NULL) {
		CDEBUG(D_RPCTRACE, "request failed to send: %p, %d\n", req,
		       req->rq_status);
		if (rc == 0)
			rc = req->rq_status ?: -EIO;
	} else if (rc == 0 || rc == -EAGAIN) {
		struct mdt_body *body;

		rc = lustre_msg_get_status(req->rq_repmsg);
		if (lustre_msg_get_type(req->rq_repmsg) == PTL_RPC_MSG_ERR) {
			DEBUG_REQ(D_ERROR, req,
				  "type == PTL_RPC_MSG_ERR, err = %d", rc);
			if (rc > 0)
				rc = -rc;
		}
		body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
		if (body == NULL)
			rc = -EPROTO;
	} else if (rc == -ESTALE) {
		/**
		 * it can be allowed error after 3633 if open was committed and
		 * server failed before close was sent. Let's check if mod
		 * exists and return no error in that case
		 */
		if (mod) {
			DEBUG_REQ(D_HA, req, "Reset ESTALE = %d", rc);
			LASSERT(mod->mod_open_req != NULL);
			if (mod->mod_open_req->rq_committed)
				rc = 0;
		}
	}

	if (mod) {
		if (rc != 0)
			mod->mod_close_req = NULL;
		/* Since now, mod is accessed through open_req only,
		 * thus close req does not keep a reference on mod anymore. */
		obd_mod_put(mod);
	}
	*request = req;
	mdc_close_handle_reply(req, op_data, rc);
	return rc < 0 ? rc : saved_rc;
}

int mdc_done_writing(struct obd_export *exp, struct md_op_data *op_data,
		     struct md_open_data *mod)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct ptlrpc_request *req;
	int		    rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_MDS_DONE_WRITING);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);
	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_DONE_WRITING);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	if (mod != NULL) {
		LASSERTF(mod->mod_open_req != NULL &&
			 mod->mod_open_req->rq_type != LI_POISON,
			 "POISONED setattr %p!\n", mod->mod_open_req);

		mod->mod_close_req = req;
		DEBUG_REQ(D_HA, mod->mod_open_req, "matched setattr");
		/* We no longer want to preserve this setattr for replay even
		 * though the open was committed. b=3632, b=3633 */
		spin_lock(&mod->mod_open_req->rq_lock);
		mod->mod_open_req->rq_replay = 0;
		spin_unlock(&mod->mod_open_req->rq_lock);
	}

	mdc_close_pack(req, op_data);
	ptlrpc_request_set_replen(req);

	mdc_get_rpc_lock(obd->u.cli.cl_close_lock, NULL);
	rc = ptlrpc_queue_wait(req);
	mdc_put_rpc_lock(obd->u.cli.cl_close_lock, NULL);

	if (rc == -ESTALE) {
		/**
		 * it can be allowed error after 3633 if open or setattr were
		 * committed and server failed before close was sent.
		 * Let's check if mod exists and return no error in that case
		 */
		if (mod) {
			LASSERT(mod->mod_open_req != NULL);
			if (mod->mod_open_req->rq_committed)
				rc = 0;
		}
	}

	if (mod) {
		if (rc != 0)
			mod->mod_close_req = NULL;
		LASSERT(mod->mod_open_req != NULL);
		mdc_free_open(mod);

		/* Since now, mod is accessed through setattr req only,
		 * thus DW req does not keep a reference on mod anymore. */
		obd_mod_put(mod);
	}

	mdc_close_handle_reply(req, op_data, rc);
	ptlrpc_req_finished(req);
	return rc;
}


int mdc_readpage(struct obd_export *exp, struct md_op_data *op_data,
		 struct page **pages, struct ptlrpc_request **request)
{
	struct ptlrpc_request   *req;
	struct ptlrpc_bulk_desc *desc;
	int		      i;
	wait_queue_head_t	      waitq;
	int		      resends = 0;
	struct l_wait_info       lwi;
	int		      rc;

	*request = NULL;
	init_waitqueue_head(&waitq);

restart_bulk:
	req = ptlrpc_request_alloc(class_exp2cliimp(exp), &RQF_MDS_READPAGE);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_READPAGE);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	req->rq_request_portal = MDS_READPAGE_PORTAL;
	ptlrpc_at_set_req_timeout(req);

	desc = ptlrpc_prep_bulk_imp(req, op_data->op_npages, 1, BULK_PUT_SINK,
				    MDS_BULK_PORTAL);
	if (desc == NULL) {
		ptlrpc_request_free(req);
		return -ENOMEM;
	}

	/* NB req now owns desc and will free it when it gets freed */
	for (i = 0; i < op_data->op_npages; i++)
		ptlrpc_prep_bulk_page_pin(desc, pages[i], 0, PAGE_CACHE_SIZE);

	mdc_readdir_pack(req, op_data->op_offset,
			 PAGE_CACHE_SIZE * op_data->op_npages,
			 &op_data->op_fid1, op_data->op_capa1);

	ptlrpc_request_set_replen(req);
	rc = ptlrpc_queue_wait(req);
	if (rc) {
		ptlrpc_req_finished(req);
		if (rc != -ETIMEDOUT)
			return rc;

		resends++;
		if (!client_should_resend(resends, &exp->exp_obd->u.cli)) {
			CERROR("too many resend retries, returning error\n");
			return -EIO;
		}
		lwi = LWI_TIMEOUT_INTR(cfs_time_seconds(resends),
				       NULL, NULL, NULL);
		l_wait_event(waitq, 0, &lwi);

		goto restart_bulk;
	}

	rc = sptlrpc_cli_unwrap_bulk_read(req, req->rq_bulk,
					  req->rq_bulk->bd_nob_transferred);
	if (rc < 0) {
		ptlrpc_req_finished(req);
		return rc;
	}

	if (req->rq_bulk->bd_nob_transferred & ~LU_PAGE_MASK) {
		CERROR("Unexpected # bytes transferred: %d (%ld expected)\n",
			req->rq_bulk->bd_nob_transferred,
			PAGE_CACHE_SIZE * op_data->op_npages);
		ptlrpc_req_finished(req);
		return -EPROTO;
	}

	*request = req;
	return 0;
}

static int mdc_statfs(const struct lu_env *env,
		      struct obd_export *exp, struct obd_statfs *osfs,
		      __u64 max_age, __u32 flags)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct ptlrpc_request *req;
	struct obd_statfs     *msfs;
	struct obd_import     *imp = NULL;
	int		    rc;

	/*
	 * Since the request might also come from lprocfs, so we need
	 * sync this with client_disconnect_export Bug15684
	 */
	down_read(&obd->u.cli.cl_sem);
	if (obd->u.cli.cl_import)
		imp = class_import_get(obd->u.cli.cl_import);
	up_read(&obd->u.cli.cl_sem);
	if (!imp)
		return -ENODEV;

	req = ptlrpc_request_alloc_pack(imp, &RQF_MDS_STATFS,
					LUSTRE_MDS_VERSION, MDS_STATFS);
	if (req == NULL) {
		rc = -ENOMEM;
		goto output;
	}

	ptlrpc_request_set_replen(req);

	if (flags & OBD_STATFS_NODELAY) {
		/* procfs requests not want stay in wait for avoid deadlock */
		req->rq_no_resend = 1;
		req->rq_no_delay = 1;
	}

	rc = ptlrpc_queue_wait(req);
	if (rc) {
		/* check connection error first */
		if (imp->imp_connect_error)
			rc = imp->imp_connect_error;
		goto out;
	}

	msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
	if (msfs == NULL) {
		rc = -EPROTO;
		goto out;
	}

	*osfs = *msfs;
out:
	ptlrpc_req_finished(req);
output:
	class_import_put(imp);
	return rc;
}

static int mdc_ioc_fid2path(struct obd_export *exp, struct getinfo_fid2path *gf)
{
	__u32 keylen, vallen;
	void *key;
	int rc;

	if (gf->gf_pathlen > PATH_MAX)
		return -ENAMETOOLONG;
	if (gf->gf_pathlen < 2)
		return -EOVERFLOW;

	/* Key is KEY_FID2PATH + getinfo_fid2path description */
	keylen = cfs_size_round(sizeof(KEY_FID2PATH)) + sizeof(*gf);
	OBD_ALLOC(key, keylen);
	if (key == NULL)
		return -ENOMEM;
	memcpy(key, KEY_FID2PATH, sizeof(KEY_FID2PATH));
	memcpy(key + cfs_size_round(sizeof(KEY_FID2PATH)), gf, sizeof(*gf));

	CDEBUG(D_IOCTL, "path get "DFID" from %llu #%d\n",
	       PFID(&gf->gf_fid), gf->gf_recno, gf->gf_linkno);

	if (!fid_is_sane(&gf->gf_fid)) {
		rc = -EINVAL;
		goto out;
	}

	/* Val is struct getinfo_fid2path result plus path */
	vallen = sizeof(*gf) + gf->gf_pathlen;

	rc = obd_get_info(NULL, exp, keylen, key, &vallen, gf, NULL);
	if (rc != 0 && rc != -EREMOTE)
		goto out;

	if (vallen <= sizeof(*gf)) {
		rc = -EPROTO;
		goto out;
	} else if (vallen > sizeof(*gf) + gf->gf_pathlen) {
		rc = -EOVERFLOW;
		goto out;
	}

	CDEBUG(D_IOCTL, "path get "DFID" from %llu #%d\n%s\n",
	       PFID(&gf->gf_fid), gf->gf_recno, gf->gf_linkno, gf->gf_path);

out:
	OBD_FREE(key, keylen);
	return rc;
}

static int mdc_ioc_hsm_progress(struct obd_export *exp,
				struct hsm_progress_kernel *hpk)
{
	struct obd_import		*imp = class_exp2cliimp(exp);
	struct hsm_progress_kernel	*req_hpk;
	struct ptlrpc_request		*req;
	int				 rc;

	req = ptlrpc_request_alloc_pack(imp, &RQF_MDS_HSM_PROGRESS,
					LUSTRE_MDS_VERSION, MDS_HSM_PROGRESS);
	if (req == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	mdc_pack_body(req, NULL, NULL, OBD_MD_FLRMTPERM, 0, 0, 0);

	/* Copy hsm_progress struct */
	req_hpk = req_capsule_client_get(&req->rq_pill, &RMF_MDS_HSM_PROGRESS);
	if (req_hpk == NULL) {
		rc = -EPROTO;
		goto out;
	}

	*req_hpk = *hpk;
	req_hpk->hpk_errval = lustre_errno_hton(hpk->hpk_errval);

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	goto out;
out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_ioc_hsm_ct_register(struct obd_import *imp, __u32 archives)
{
	__u32			*archive_mask;
	struct ptlrpc_request	*req;
	int			 rc;

	req = ptlrpc_request_alloc_pack(imp, &RQF_MDS_HSM_CT_REGISTER,
					LUSTRE_MDS_VERSION,
					MDS_HSM_CT_REGISTER);
	if (req == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	mdc_pack_body(req, NULL, NULL, OBD_MD_FLRMTPERM, 0, 0, 0);

	/* Copy hsm_progress struct */
	archive_mask = req_capsule_client_get(&req->rq_pill,
					      &RMF_MDS_HSM_ARCHIVE);
	if (archive_mask == NULL) {
		rc = -EPROTO;
		goto out;
	}

	*archive_mask = archives;

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	goto out;
out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_ioc_hsm_current_action(struct obd_export *exp,
				      struct md_op_data *op_data)
{
	struct hsm_current_action	*hca = op_data->op_data;
	struct hsm_current_action	*req_hca;
	struct ptlrpc_request		*req;
	int				 rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_MDS_HSM_ACTION);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_HSM_ACTION);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_pack_body(req, &op_data->op_fid1, op_data->op_capa1,
		      OBD_MD_FLRMTPERM, 0, op_data->op_suppgids[0], 0);

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	if (rc)
		goto out;

	req_hca = req_capsule_server_get(&req->rq_pill,
					 &RMF_MDS_HSM_CURRENT_ACTION);
	if (req_hca == NULL) {
		rc = -EPROTO;
		goto out;
	}

	*hca = *req_hca;

out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_ioc_hsm_ct_unregister(struct obd_import *imp)
{
	struct ptlrpc_request	*req;
	int			 rc;

	req = ptlrpc_request_alloc_pack(imp, &RQF_MDS_HSM_CT_UNREGISTER,
					LUSTRE_MDS_VERSION,
					MDS_HSM_CT_UNREGISTER);
	if (req == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	mdc_pack_body(req, NULL, NULL, OBD_MD_FLRMTPERM, 0, 0, 0);

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	goto out;
out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_ioc_hsm_state_get(struct obd_export *exp,
				 struct md_op_data *op_data)
{
	struct hsm_user_state	*hus = op_data->op_data;
	struct hsm_user_state	*req_hus;
	struct ptlrpc_request	*req;
	int			 rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_MDS_HSM_STATE_GET);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_HSM_STATE_GET);
	if (rc != 0) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_pack_body(req, &op_data->op_fid1, op_data->op_capa1,
		      OBD_MD_FLRMTPERM, 0, op_data->op_suppgids[0], 0);

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	if (rc)
		goto out;

	req_hus = req_capsule_server_get(&req->rq_pill, &RMF_HSM_USER_STATE);
	if (req_hus == NULL) {
		rc = -EPROTO;
		goto out;
	}

	*hus = *req_hus;

out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_ioc_hsm_state_set(struct obd_export *exp,
				 struct md_op_data *op_data)
{
	struct hsm_state_set	*hss = op_data->op_data;
	struct hsm_state_set	*req_hss;
	struct ptlrpc_request	*req;
	int			 rc;

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_MDS_HSM_STATE_SET);
	if (req == NULL)
		return -ENOMEM;

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_HSM_STATE_SET);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_pack_body(req, &op_data->op_fid1, op_data->op_capa1,
		      OBD_MD_FLRMTPERM, 0, op_data->op_suppgids[0], 0);

	/* Copy states */
	req_hss = req_capsule_client_get(&req->rq_pill, &RMF_HSM_STATE_SET);
	if (req_hss == NULL) {
		rc = -EPROTO;
		goto out;
	}
	*req_hss = *hss;

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	goto out;

out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_ioc_hsm_request(struct obd_export *exp,
			       struct hsm_user_request *hur)
{
	struct obd_import	*imp = class_exp2cliimp(exp);
	struct ptlrpc_request	*req;
	struct hsm_request	*req_hr;
	struct hsm_user_item	*req_hui;
	char			*req_opaque;
	int			 rc;

	req = ptlrpc_request_alloc(imp, &RQF_MDS_HSM_REQUEST);
	if (req == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	req_capsule_set_size(&req->rq_pill, &RMF_MDS_HSM_USER_ITEM, RCL_CLIENT,
			     hur->hur_request.hr_itemcount
			     * sizeof(struct hsm_user_item));
	req_capsule_set_size(&req->rq_pill, &RMF_GENERIC_DATA, RCL_CLIENT,
			     hur->hur_request.hr_data_len);

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_HSM_REQUEST);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_pack_body(req, NULL, NULL, OBD_MD_FLRMTPERM, 0, 0, 0);

	/* Copy hsm_request struct */
	req_hr = req_capsule_client_get(&req->rq_pill, &RMF_MDS_HSM_REQUEST);
	if (req_hr == NULL) {
		rc = -EPROTO;
		goto out;
	}
	*req_hr = hur->hur_request;

	/* Copy hsm_user_item structs */
	req_hui = req_capsule_client_get(&req->rq_pill, &RMF_MDS_HSM_USER_ITEM);
	if (req_hui == NULL) {
		rc = -EPROTO;
		goto out;
	}
	memcpy(req_hui, hur->hur_user_item,
	       hur->hur_request.hr_itemcount * sizeof(struct hsm_user_item));

	/* Copy opaque field */
	req_opaque = req_capsule_client_get(&req->rq_pill, &RMF_GENERIC_DATA);
	if (req_opaque == NULL) {
		rc = -EPROTO;
		goto out;
	}
	memcpy(req_opaque, hur_data(hur), hur->hur_request.hr_data_len);

	ptlrpc_request_set_replen(req);

	rc = mdc_queue_wait(req);
	goto out;

out:
	ptlrpc_req_finished(req);
	return rc;
}

static struct kuc_hdr *changelog_kuc_hdr(char *buf, int len, int flags)
{
	struct kuc_hdr *lh = (struct kuc_hdr *)buf;

	LASSERT(len <= KUC_CHANGELOG_MSG_MAXSIZE);

	lh->kuc_magic = KUC_MAGIC;
	lh->kuc_transport = KUC_TRANSPORT_CHANGELOG;
	lh->kuc_flags = flags;
	lh->kuc_msgtype = CL_RECORD;
	lh->kuc_msglen = len;
	return lh;
}

#define D_CHANGELOG 0

struct changelog_show {
	__u64		cs_startrec;
	__u32		cs_flags;
	struct file	*cs_fp;
	char		*cs_buf;
	struct obd_device *cs_obd;
};

static int changelog_kkuc_cb(const struct lu_env *env, struct llog_handle *llh,
			     struct llog_rec_hdr *hdr, void *data)
{
	struct changelog_show *cs = data;
	struct llog_changelog_rec *rec = (struct llog_changelog_rec *)hdr;
	struct kuc_hdr *lh;
	int len, rc;

	if (rec->cr_hdr.lrh_type != CHANGELOG_REC) {
		rc = -EINVAL;
		CERROR("%s: not a changelog rec %x/%d: rc = %d\n",
		       cs->cs_obd->obd_name, rec->cr_hdr.lrh_type,
		       rec->cr.cr_type, rc);
		return rc;
	}

	if (rec->cr.cr_index < cs->cs_startrec) {
		/* Skip entries earlier than what we are interested in */
		CDEBUG(D_CHANGELOG, "rec=%llu start=%llu\n",
		       rec->cr.cr_index, cs->cs_startrec);
		return 0;
	}

	CDEBUG(D_CHANGELOG, "%llu %02d%-5s %llu 0x%x t="DFID" p="DFID
		" %.*s\n", rec->cr.cr_index, rec->cr.cr_type,
		changelog_type2str(rec->cr.cr_type), rec->cr.cr_time,
		rec->cr.cr_flags & CLF_FLAGMASK,
		PFID(&rec->cr.cr_tfid), PFID(&rec->cr.cr_pfid),
		rec->cr.cr_namelen, changelog_rec_name(&rec->cr));

	len = sizeof(*lh) + changelog_rec_size(&rec->cr) + rec->cr.cr_namelen;

	/* Set up the message */
	lh = changelog_kuc_hdr(cs->cs_buf, len, cs->cs_flags);
	memcpy(lh + 1, &rec->cr, len - sizeof(*lh));

	rc = libcfs_kkuc_msg_put(cs->cs_fp, lh);
	CDEBUG(D_CHANGELOG, "kucmsg fp %p len %d rc %d\n", cs->cs_fp, len, rc);

	return rc;
}

static int mdc_changelog_send_thread(void *csdata)
{
	struct changelog_show *cs = csdata;
	struct llog_ctxt *ctxt = NULL;
	struct llog_handle *llh = NULL;
	struct kuc_hdr *kuch;
	int rc;

	CDEBUG(D_CHANGELOG, "changelog to fp=%p start %llu\n",
	       cs->cs_fp, cs->cs_startrec);

	OBD_ALLOC(cs->cs_buf, KUC_CHANGELOG_MSG_MAXSIZE);
	if (cs->cs_buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/* Set up the remote catalog handle */
	ctxt = llog_get_context(cs->cs_obd, LLOG_CHANGELOG_REPL_CTXT);
	if (ctxt == NULL) {
		rc = -ENOENT;
		goto out;
	}
	rc = llog_open(NULL, ctxt, &llh, NULL, CHANGELOG_CATALOG,
		       LLOG_OPEN_EXISTS);
	if (rc) {
		CERROR("%s: fail to open changelog catalog: rc = %d\n",
		       cs->cs_obd->obd_name, rc);
		goto out;
	}
	rc = llog_init_handle(NULL, llh, LLOG_F_IS_CAT, NULL);
	if (rc) {
		CERROR("llog_init_handle failed %d\n", rc);
		goto out;
	}

	rc = llog_cat_process(NULL, llh, changelog_kkuc_cb, cs, 0, 0);

	/* Send EOF no matter what our result */
	kuch = changelog_kuc_hdr(cs->cs_buf, sizeof(*kuch), cs->cs_flags);
	if (kuch) {
		kuch->kuc_msgtype = CL_EOF;
		libcfs_kkuc_msg_put(cs->cs_fp, kuch);
	}

out:
	fput(cs->cs_fp);
	if (llh)
		llog_cat_close(NULL, llh);
	if (ctxt)
		llog_ctxt_put(ctxt);
	if (cs->cs_buf)
		OBD_FREE(cs->cs_buf, KUC_CHANGELOG_MSG_MAXSIZE);
	OBD_FREE_PTR(cs);
	return rc;
}

static int mdc_ioc_changelog_send(struct obd_device *obd,
				  struct ioc_changelog *icc)
{
	struct changelog_show *cs;
	int rc;

	/* Freed in mdc_changelog_send_thread */
	OBD_ALLOC_PTR(cs);
	if (!cs)
		return -ENOMEM;

	cs->cs_obd = obd;
	cs->cs_startrec = icc->icc_recno;
	/* matching fput in mdc_changelog_send_thread */
	cs->cs_fp = fget(icc->icc_id);
	cs->cs_flags = icc->icc_flags;

	/*
	 * New thread because we should return to user app before
	 * writing into our pipe
	 */
	rc = PTR_ERR(kthread_run(mdc_changelog_send_thread, cs,
				 "mdc_clg_send_thread"));
	if (!IS_ERR_VALUE(rc)) {
		CDEBUG(D_CHANGELOG, "start changelog thread\n");
		return 0;
	}

	CERROR("Failed to start changelog thread: %d\n", rc);
	OBD_FREE_PTR(cs);
	return rc;
}

static int mdc_ioc_hsm_ct_start(struct obd_export *exp,
				struct lustre_kernelcomm *lk);

static int mdc_quotacheck(struct obd_device *unused, struct obd_export *exp,
			  struct obd_quotactl *oqctl)
{
	struct client_obd       *cli = &exp->exp_obd->u.cli;
	struct ptlrpc_request   *req;
	struct obd_quotactl     *body;
	int		      rc;

	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp),
					&RQF_MDS_QUOTACHECK, LUSTRE_MDS_VERSION,
					MDS_QUOTACHECK);
	if (req == NULL)
		return -ENOMEM;

	body = req_capsule_client_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
	*body = *oqctl;

	ptlrpc_request_set_replen(req);

	/* the next poll will find -ENODATA, that means quotacheck is
	 * going on */
	cli->cl_qchk_stat = -ENODATA;
	rc = ptlrpc_queue_wait(req);
	if (rc)
		cli->cl_qchk_stat = rc;
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_quota_poll_check(struct obd_export *exp,
				struct if_quotacheck *qchk)
{
	struct client_obd *cli = &exp->exp_obd->u.cli;
	int rc;

	qchk->obd_uuid = cli->cl_target_uuid;
	memcpy(qchk->obd_type, LUSTRE_MDS_NAME, strlen(LUSTRE_MDS_NAME));

	rc = cli->cl_qchk_stat;
	/* the client is not the previous one */
	if (rc == CL_NOT_QUOTACHECKED)
		rc = -EINTR;
	return rc;
}

static int mdc_quotactl(struct obd_device *unused, struct obd_export *exp,
			struct obd_quotactl *oqctl)
{
	struct ptlrpc_request   *req;
	struct obd_quotactl     *oqc;
	int		      rc;

	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp),
					&RQF_MDS_QUOTACTL, LUSTRE_MDS_VERSION,
					MDS_QUOTACTL);
	if (req == NULL)
		return -ENOMEM;

	oqc = req_capsule_client_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
	*oqc = *oqctl;

	ptlrpc_request_set_replen(req);
	ptlrpc_at_set_req_timeout(req);
	req->rq_no_resend = 1;

	rc = ptlrpc_queue_wait(req);
	if (rc)
		CERROR("ptlrpc_queue_wait failed, rc: %d\n", rc);

	if (req->rq_repmsg) {
		oqc = req_capsule_server_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
		if (oqc) {
			*oqctl = *oqc;
		} else if (!rc) {
			CERROR("Can't unpack obd_quotactl\n");
			rc = -EPROTO;
		}
	} else if (!rc) {
		CERROR("Can't unpack obd_quotactl\n");
		rc = -EPROTO;
	}
	ptlrpc_req_finished(req);

	return rc;
}

static int mdc_ioc_swap_layouts(struct obd_export *exp,
				struct md_op_data *op_data)
{
	LIST_HEAD(cancels);
	struct ptlrpc_request	*req;
	int			 rc, count;
	struct mdc_swap_layouts *msl, *payload;

	msl = op_data->op_data;

	/* When the MDT will get the MDS_SWAP_LAYOUTS RPC the
	 * first thing it will do is to cancel the 2 layout
	 * locks hold by this client.
	 * So the client must cancel its layout locks on the 2 fids
	 * with the request RPC to avoid extra RPC round trips
	 */
	count = mdc_resource_get_unused(exp, &op_data->op_fid1, &cancels,
					LCK_CR, MDS_INODELOCK_LAYOUT);
	count += mdc_resource_get_unused(exp, &op_data->op_fid2, &cancels,
					 LCK_CR, MDS_INODELOCK_LAYOUT);

	req = ptlrpc_request_alloc(class_exp2cliimp(exp),
				   &RQF_MDS_SWAP_LAYOUTS);
	if (req == NULL) {
		ldlm_lock_list_put(&cancels, l_bl_ast, count);
		return -ENOMEM;
	}

	mdc_set_capa_size(req, &RMF_CAPA1, op_data->op_capa1);
	mdc_set_capa_size(req, &RMF_CAPA2, op_data->op_capa2);

	rc = mdc_prep_elc_req(exp, req, MDS_SWAP_LAYOUTS, &cancels, count);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	mdc_swap_layouts_pack(req, op_data);

	payload = req_capsule_client_get(&req->rq_pill, &RMF_SWAP_LAYOUTS);
	LASSERT(payload);

	*payload = *msl;

	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	if (rc)
		goto out;

out:
	ptlrpc_req_finished(req);
	return rc;
}

static int mdc_iocontrol(unsigned int cmd, struct obd_export *exp, int len,
			 void *karg, void *uarg)
{
	struct obd_device *obd = exp->exp_obd;
	struct obd_ioctl_data *data = karg;
	struct obd_import *imp = obd->u.cli.cl_import;
	int rc;

	if (!try_module_get(THIS_MODULE)) {
		CERROR("Can't get module. Is it alive?");
		return -EINVAL;
	}
	switch (cmd) {
	case OBD_IOC_CHANGELOG_SEND:
		rc = mdc_ioc_changelog_send(obd, karg);
		goto out;
	case OBD_IOC_CHANGELOG_CLEAR: {
		struct ioc_changelog *icc = karg;
		struct changelog_setinfo cs = {
			.cs_recno = icc->icc_recno,
			.cs_id = icc->icc_id
		};

		rc = obd_set_info_async(NULL, exp, strlen(KEY_CHANGELOG_CLEAR),
					KEY_CHANGELOG_CLEAR, sizeof(cs), &cs,
					NULL);
		goto out;
	}
	case OBD_IOC_FID2PATH:
		rc = mdc_ioc_fid2path(exp, karg);
		goto out;
	case LL_IOC_HSM_CT_START:
		rc = mdc_ioc_hsm_ct_start(exp, karg);
		/* ignore if it was already registered on this MDS. */
		if (rc == -EEXIST)
			rc = 0;
		goto out;
	case LL_IOC_HSM_PROGRESS:
		rc = mdc_ioc_hsm_progress(exp, karg);
		goto out;
	case LL_IOC_HSM_STATE_GET:
		rc = mdc_ioc_hsm_state_get(exp, karg);
		goto out;
	case LL_IOC_HSM_STATE_SET:
		rc = mdc_ioc_hsm_state_set(exp, karg);
		goto out;
	case LL_IOC_HSM_ACTION:
		rc = mdc_ioc_hsm_current_action(exp, karg);
		goto out;
	case LL_IOC_HSM_REQUEST:
		rc = mdc_ioc_hsm_request(exp, karg);
		goto out;
	case OBD_IOC_CLIENT_RECOVER:
		rc = ptlrpc_recover_import(imp, data->ioc_inlbuf1, 0);
		if (rc < 0)
			goto out;
		rc = 0;
		goto out;
	case IOC_OSC_SET_ACTIVE:
		rc = ptlrpc_set_import_active(imp, data->ioc_offset);
		goto out;
	case OBD_IOC_POLL_QUOTACHECK:
		rc = mdc_quota_poll_check(exp, (struct if_quotacheck *)karg);
		goto out;
	case OBD_IOC_PING_TARGET:
		rc = ptlrpc_obd_ping(obd);
		goto out;
	/*
	 * Normally IOC_OBD_STATFS, OBD_IOC_QUOTACTL iocontrol are handled by
	 * LMV instead of MDC. But when the cluster is upgraded from 1.8,
	 * there'd be no LMV layer thus we might be called here. Eventually
	 * this code should be removed.
	 * bz20731, LU-592.
	 */
	case IOC_OBD_STATFS: {
		struct obd_statfs stat_buf = {0};

		if (*((__u32 *) data->ioc_inlbuf2) != 0) {
			rc = -ENODEV;
			goto out;
		}

		/* copy UUID */
		if (copy_to_user(data->ioc_pbuf2, obd2cli_tgt(obd),
				     min((int) data->ioc_plen2,
					 (int) sizeof(struct obd_uuid)))) {
			rc = -EFAULT;
			goto out;
		}

		rc = mdc_statfs(NULL, obd->obd_self_export, &stat_buf,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				0);
		if (rc != 0)
			goto out;

		if (copy_to_user(data->ioc_pbuf1, &stat_buf,
				     min((int) data->ioc_plen1,
					 (int) sizeof(stat_buf)))) {
			rc = -EFAULT;
			goto out;
		}

		rc = 0;
		goto out;
	}
	case OBD_IOC_QUOTACTL: {
		struct if_quotactl *qctl = karg;
		struct obd_quotactl *oqctl;

		OBD_ALLOC_PTR(oqctl);
		if (oqctl == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		QCTL_COPY(oqctl, qctl);
		rc = obd_quotactl(exp, oqctl);
		if (rc == 0) {
			QCTL_COPY(qctl, oqctl);
			qctl->qc_valid = QC_MDTIDX;
			qctl->obd_uuid = obd->u.cli.cl_target_uuid;
		}

		OBD_FREE_PTR(oqctl);
		goto out;
	}
	case LL_IOC_GET_CONNECT_FLAGS:
		if (copy_to_user(uarg, exp_connect_flags_ptr(exp),
				 sizeof(*exp_connect_flags_ptr(exp)))) {
			rc = -EFAULT;
			goto out;
		}

		rc = 0;
		goto out;
	case LL_IOC_LOV_SWAP_LAYOUTS:
		rc = mdc_ioc_swap_layouts(exp, karg);
		goto out;
	default:
		CERROR("unrecognised ioctl: cmd = %#x\n", cmd);
		rc = -ENOTTY;
		goto out;
	}
out:
	module_put(THIS_MODULE);

	return rc;
}

int mdc_get_info_rpc(struct obd_export *exp,
		     u32 keylen, void *key,
		     int vallen, void *val)
{
	struct obd_import      *imp = class_exp2cliimp(exp);
	struct ptlrpc_request  *req;
	char		   *tmp;
	int		     rc = -EINVAL;

	req = ptlrpc_request_alloc(imp, &RQF_MDS_GET_INFO);
	if (req == NULL)
		return -ENOMEM;

	req_capsule_set_size(&req->rq_pill, &RMF_GETINFO_KEY,
			     RCL_CLIENT, keylen);
	req_capsule_set_size(&req->rq_pill, &RMF_GETINFO_VALLEN,
			     RCL_CLIENT, sizeof(__u32));

	rc = ptlrpc_request_pack(req, LUSTRE_MDS_VERSION, MDS_GET_INFO);
	if (rc) {
		ptlrpc_request_free(req);
		return rc;
	}

	tmp = req_capsule_client_get(&req->rq_pill, &RMF_GETINFO_KEY);
	memcpy(tmp, key, keylen);
	tmp = req_capsule_client_get(&req->rq_pill, &RMF_GETINFO_VALLEN);
	memcpy(tmp, &vallen, sizeof(__u32));

	req_capsule_set_size(&req->rq_pill, &RMF_GETINFO_VAL,
			     RCL_SERVER, vallen);
	ptlrpc_request_set_replen(req);

	rc = ptlrpc_queue_wait(req);
	/* -EREMOTE means the get_info result is partial, and it needs to
	 * continue on another MDT, see fid2path part in lmv_iocontrol */
	if (rc == 0 || rc == -EREMOTE) {
		tmp = req_capsule_server_get(&req->rq_pill, &RMF_GETINFO_VAL);
		memcpy(val, tmp, vallen);
		if (ptlrpc_rep_need_swab(req)) {
			if (KEY_IS(KEY_FID2PATH))
				lustre_swab_fid2path(val);
		}
	}
	ptlrpc_req_finished(req);

	return rc;
}

static void lustre_swab_hai(struct hsm_action_item *h)
{
	__swab32s(&h->hai_len);
	__swab32s(&h->hai_action);
	lustre_swab_lu_fid(&h->hai_fid);
	lustre_swab_lu_fid(&h->hai_dfid);
	__swab64s(&h->hai_cookie);
	__swab64s(&h->hai_extent.offset);
	__swab64s(&h->hai_extent.length);
	__swab64s(&h->hai_gid);
}

static void lustre_swab_hal(struct hsm_action_list *h)
{
	struct hsm_action_item	*hai;
	int			 i;

	__swab32s(&h->hal_version);
	__swab32s(&h->hal_count);
	__swab32s(&h->hal_archive_id);
	__swab64s(&h->hal_flags);
	hai = hai_zero(h);
	for (i = 0; i < h->hal_count; i++, hai = hai_next(= h% lrq_pill, o;
		}
n_		lit flags)
{
	struclwab_hai(str162s(l KUC_CHANGEeq);

	N);8 l

	lh->kuc_magice fiai(str162s(l KUC_CHlags);t);
	__swa162s(l KUC_CHlaon_ite;
	OBD_FREE_PTR(cs);
	return rc;
}

static int mdc_ioc_hsm_ct_start(struct obd_export *exp,
ey,
		     int vallen,  *val)
{
	struct obd_import    log_sho->rq->hal_cd %dk->requ
	struuct obd_imp open_data *dk->reqgroup_CAC->csGRP, ka	if (!try_moduBtro LL_to segroup_R("ptlrdk->reqgroup("Can't get module. Is i
	struct ku ka, "CT	returnr%d w%d u%d g%d fERROR("udk->reqrfd"udk->reqwfd"changelogdk->reqobdlrdk->reqgrouplrdk->req_archivedata *dk->req), rec->LKAU;
t_bOPec->cr.crUc;
}

stats layout h->rdinatal_allocate a FID);
	return rc;
}

stat(req);
k(&mod->modate a FID);
	return ;
}

stat(re,q->hal_cbcfs_kk}
	}
	ptlrpc_re NUL changelac->cr.cr_ 2 fny ustreR("to LL_to ssL ch@nueam_GETC->cc->cr.cr_(
{
	stru+tatic void lustr)L ch@nueam_DEBUtoturnfset);ol a->cr.cr
for the repE_PTR(csreturLL_to sOG_SENrec *)hdrn, void *key,
		     
{
	str		en, int flags)
{
	structd *;_hal(struct hsm_actiustrist l int flags)atic void lustre_)_buf, l)h)
{
	struimp = obd-n -ENAlog_rec_name(&log_rec_t l
	if (!try_moduShlen, kac->cr.cr_%d <_R("ptlrdmd, stangelogmin((i(log_rec_name(&log_rec_t l
	("Can't get monpack obd_qobd-n= KUC_CHANGELO= 	__swa16(_MSG_MAXS
	if (!l, o;
		}
n_		litif (ll	64s(&h->hai_gid)hPATH))tactl\n");
n= KUC_CHANGEL!OG_MSG_MAXS	if (!try_moduBtroHANGEL%x!=%ROR("ud= KUC_CHANGE,G_MSG_MAXS	"Can't get monpack obd_
	struct ku ka,changelog"Recel_cdc->cr.cr_mg=CHANG%d mG%d lG%d  void sG%d al, "path get "DFId= KUC_CHANGE,G);

	lh->kuc_magi"ud= KUC_CHlags);h get "DFId= KUC_CHlaon_for l= hai_zero(for l= hai_fs %d\;

	ptlrBroadout seta kacustreR
	iefore
	 * 
		kuch->kucgroupOTTY;->csGRP, kac = libk}
	}
	ptlrpc_re NUL chbe nbse ifunreq);
paod);

o)
{
ger wan-;
}

sta("tooacha kacrLL_to sL chrunn, that MDC,	/**
		 op_shutdisc/OC_CLIEy.L ch@nueam_exp_o->hal_cdior setadepares trLL_to sL ch@nueam_cbh->ghbe nbse i->gumld b(int vallen)
for the repE_PTR(csreturn ;
;
}

statstatfs
	mdc_ic int bh->g     struct hsm_user_request *tic int mdc_ioc_hsm_) bh->g   log_sho	q->hal_cd %u
	struuct o	NULL;
	struct kud_cl"OC_CLIEo LL_to se;
}

sraeq);

o) op_      &R=ERR)path get "DFI->hal_cbcfsate a FID);
	return ;
}

stat(re,q->hal_cbcf
dc_ioc_hsm_heck iffres trLL_to se_opep, karg);
		/* ignr_item}
	pt(VERSION, M&&
		ptlrpc_ed on )req, op_0pc_re NUL chRe-_evebusth= -E)
{
gtalog h lprocf opL ch/**
		 op_shutdisc/OC_CLIEy.L cr the repE_PTR(cs>kuc;
;
}

static int mdc_ioc_hsm_ct_unregc_ian-;
}

staa kac.crnst;

	/* ECORD;		kuch->kucgroupOe weach;->csGRP, kac R(csreturn ;
;
}

sta			     mic int)(req);e_put(THIS_d = icc->icc_idturn 0;
}

static int mdc_statfs(cotactl(struct obd_device *uatfs(coc(struct obd_export *exp,
		  coc(st32 keylen, void *c_statfs(cotactl(sL,
			     RCL_SER *e);
    struct hsm_user_request *hur)
{
	struct obd_impor{
	struimp = obd-rpc_rep_nee}

	_ONLY)ted)
				r-EPROTO!gelog_recin((shed(req);
	
	char		   as committed(imp->impspin_lockct obd_ut(Tctd *);
		if (imp->imp_connectr(exp)origGETAbd->
		goto RDONLY;	if (imp->imp_connectu
	s.ocdp_connectr(expGET		     obd->
		goto RDONLY;	ifk(&mod->mod (imp->imp_connectr(exp)origG&= ~bd->
		goto RDONLY;	if (imp->imp_connectu
	s.ocdp_connectr(expG&T		     o~bd->
		goto RDONLY;	if}
od->mod_open_re(imp->impspin_lomodate ado_d = icc->icc_id(re,q	rc =ck(req,iimp(exp),
					&RQF_MDS_Qatfs(co;

	rc = obd_32 keylenll, _import_ {
		ptlrpc_re obd-rpc_rep_neeSPTLD_ER
		F);
		if

		goto onf key, keadapEQUES)
{
	struGELOG, "start chan (ptlrpc_rep_needmp(Ht(cs);
		if

		gotoSC_SET_f64sh_myose(t(req);
G, "start chan (ptlrpc_rep_neeinfo_async(NULL,LOG_REPL_CTdo_d = icc->icc_id(re,q	rc =ck(req,iimp(exp),
					&RQF_MDS_Qatfs(co;

	rc = obd_32 keylenll, _import_ {
		ptlrpc_re obd-rpc_rep_nee kargOPYthleh (cm,LOG_REPL_CTR(csreturLL_to sOG_SEN32 keylenll (rc) {
		ptlrpc_requtransferreknisc= ob, "path struct ) obt;

out:
	p
	char		 e_put(THIS_MODULE);
};

static int changelog_kkuc_cb(truct obd_device *ua struct getinfoexport *ea struc*32 keylen, void *c_staatic intov rcripe_mvoilsm
    rt *imeq;
	char		   obd-rpc_rep_neeMAX_EAcs->cted)
		t(THIlog_, * obd		    	struct ob-EPROTO!gelog_recin((shed(req);
	
	char		 SERTlog_S);
_ut(Tctd *lockct oRTlog_S>uarg)
{
	struC_MDTIDX;
	 obdR,
			     hed(arg)
{
	struC_MDTIDX;
	 obdR,
			    _CTR(    	sSERobd		    _CTd *lock*Robd		    _CTarg)
{
	struC_MDTIDX;
	 obdR,
			    );
G, "start chaactl\n");
rpc_rep_neeDect_fl_EAcs->cted)
		t(T*CL_SERVE		    	struct ob-EPROTO!gelog_recin((shed(req);
	
	char		 SECL_SERVE		    _CTd *lock*CL_SERVE		    _CTarg)
{
	struC_MDTIDX;
	CL_SERVER,
			    );
G, "start chaactl\n");
rpc_rep_neeMAX__set_sis->cted)
		t(THIlog_, * obd		     obd	struct ob-EPROTO!gelog_recin((shed(req);
	
	char		 SERTlog_S);
_ut(Tctd *lockct oRTlog_S>uarg)
{
	struC_MDTIDX;
	 obdR,
			     obd-hed(arg)
{
	struC_MDTIDX;
	 obdR,
			     obd_CTR(    	sSERobd		     obd_CTd *lock*Robd		     obd_CTarg)
{
	struC_MDTIDX;
	 obdR,
			     obd);
G, "start chaactl\n");
rpc_rep_neeDect_fl__set_sis->cted)
		t(T*CL_SERVE		     obd	struct ob-EPROTO!gelog_recin((shed(req);
	
	char		 SECL_SERVE		     obd_CTd *lock*CL_SERVE		     obd_Ched(arg)
{
	struC_MDTIDX;
	CL_SERVER,
			     obd);
G, "start chaactl\n");
rpc_rep_nee
		gt_get()U-592.
	 */
	casl_data *data =hur)
{
	struct obd_impor2.
	 */
	cas_connectu
	s->exp_obdd *lotruct ob-EPROTO!gelog_rec  strushed(req);
	
	char		   a>exp_obd(imp->imp_connectu
	s);
G, "start chaactl\n");
rpc_rep_neeTGl__sUNT()U-592bd_ut(Tctd *)  deadlock"start changePL_CTR(csMODULE);

	rethlen;

	rc = obd_*32 keylenll (r}
out:
	module_put(THIS_cc_id mdc_iocontrol(unsigned };

static int cf voif vh get "D.
	 */
	cas_apexpoc *op_data,
		 struct page **pages, struct ptlrpc_request **robd(exp);
	struct ptlrnfo       lwi;
	inLCK_CR, MDS_INODELOCK_LAYOUT);

	req = ptlrpc_reqloc(class_eYNCuest_alloc(imp, &RQF_MDS_GET_INFO);
	if (, count);
		return -ENOMEM;
	}

	mdcc			     RCL_CLIENT, sizeof(__u32));

	rc = ptlrpc_request_peYNCuest_alloVERSION, MDS_HSM_REQUEST);
	if (rc) {
		ptlrpc_request_free(req);
		ref vh oc *q, NUL-1TPERM, 0, op_data->op_suppgids[0], 0);

	ptlrpc_request_set_replen(req);

	rc = pteq);
	if (rc)
		goto out;

ctl\
	shed(req);
		return -EPROtlrpc_req_finished(req)C_SET_     Ntatic int mdc_ioc_chang .
	 */
	casl_data *date(&req->renum
	casl_data_      prest
    rt *imeq;0c_hdr *lh = (imp->impoid *d = Q;

	p alive?"prest
	return -IMP_EVg);
DIS
		to o#

	0>cr.crXXX P;

 prest -ENOoAbd-_OBD_ck.hreadd is ger wFLD
		reallocate arc = otifypoi -EREMND:
		D:
		bd->NOTIFY
DIS
		t_handle(#_SEiflocbweatq, 0,turn -IMP_EVg);
IN0;
		goU-592.
	 */
t if_quotacheck *qcruct client_otlrpcsultF64sh C_HSM_A _i(re= NULuc_ckks hold bobtain new/* tpcsultBut w setattin urn -of e need
	 */OC_Cd
	 *rver f		goto outrlen(Ls= NULL;
		L(&res 0 | if_quf64shoutrlen(Ls= _lomodate arc = otifypoi -EREMND:
		D:
		bd->NOTIFY
IN0;
		gs(resends),bweatq, 0,turn -IMP_EVg);
INar	I_gegoU-592.
	 */
_LAYO %d\sfrec_cnsid = QC_rc = %d\sfreclomod_LAYO %d\sfrec | eanup(ns);
DLMedm_LOCAL_ONLY)lomodbweatq, 0,turn -IMP_EVg);
0;
		goto out;
rc = otifypoi -EREMND:
		D:
		bd->NOTIFY
0;
		gs(resends),c_iand locks
{
g;
}

sraeq);
/**
		OC_Cd
	 *, th already regist0 this MDS.R(cs>kuc;
;
}

stat(req);
Gbweatq, urn -IMP_EVg);
OCDoto out;
rc = otifypoi -EREMND:
		D:
		bd->NOTIFY
OCDs(resends),bweatq, urn -IMP_EVg);
DE0;
		gego,turn -IMP_EVg);
0;
		gego,t,bweatq, layouts(exp, karg);reknisc=l_data prest %ROR("uprest
ds),Luct bcfs_k
out:
	module_put(THIS_gf_r_LAYOU mdc_iocontrol(unsigned atic int cf voif vh g	(struct obd_export *exp,
				      struct if_quotacheck *qchk)
{
	struct client_oatic int c	return = N*sCK_CRutrlen(Ls= (r}
out:
	ms 0 | if_qu      id(&goto os		ref vite;
	OB>ioc_plen2,
	 * (csMODU2,
	U mdc_iocontrol(unsigne	      struct if_quotacheck *qchk)
{
	struct client_}
out:
	m&cli;
	int rc;

	qchk-_re NUL chDestammsglwhed it ocks wantcane'd beq(exre
		 * coids[aySWAP_LAdua("tL chrC_CLIEy,
		n ab64enllueAYOUTS Event:
	mffres t wantcane'd beq(exre,L chr wab64eent:
	C. Br w otL cr the repE_PTR(csbeq(exOe wIOC_CLIEy(.
	 */
_LAYO want spin_    r);
npin->l_INODELOC->lrnt len, r
DLMeIBITSF_MDS_GET_I0truct chIXMgoUr);weuprert(THIn to aelotuaeq);
wMV lclusteTATFSp->exp_oesult	if C. Bile lprocf	if (he clientaelongl upgde thef (wEvens we migh;

	/*
	 s[aythefn -oif (he cliwait_(THIntee fid2panpin->l_policytu
	s.lULEgdebits.bits &>op_fid2, &cancCHANF_MDS_GET_I0trucS_GET_I1rpc_req_finished(reqINODELOCKLEgdeEST);
.
	 */
_LAYOINODELOCrobds_    r);
bds->lrnlvbKLEgdeF_MDS_s->lrnlvbKLEgde  lwi;
	iurn -EPROTO;
	}

	 */
_LAYOnllbUTS);ops LEgdeElvborg;
		.lvboEST);extra RPC round LEgdeEST);,_buf;
	struct obra RCAT, NULLNtatic int mdc_ioc_chan
    struct hsm_remoteroup	*olg *qcruct hsm_olg;ruct changelog_sho	 *cs por{
	struimp = led %d\n",uppup(goto out; oulgtxt = llog_get_context(cs- out; this&gelog_ if_quopseq);

	rc = ptreq);
		if (rup the remoteroupOe catp t(ulgtxt = llog_get_context(cs-;
	luAT, NULLiatalp_connec llh);
	it_close(NULL, llh);
	urn -EPROTO;
	}

	*reqn, vora RCAT, c)
		gNtatic int mdc_ioc_chan
    struct gelog_show *cs f (rup the remote catalog ha
	ctxt = llog_get_context(cs->cs_obd, LLOg_cat_clos eanup(;
		goto oite;
	OBD_FREE_PTR(csuppup(tatic int mdc_ioc_chang .
	 */
64s(&h-cfth cft	      struct if_quotacheck *qcruct client_oatic intags & _OBD_FROnlrs lnlrs g;
lwi;
 };port *imp =   cs->cs_fpli;
	in;
	iUTS)ch = changpli;
	in;
	iUTS)d, cs,
			pli;
	in;
	iUTS)d_MDS_GET_INFO);
	ifuest_NULL);
	iUTS)fpli;
	in;
	iUTS)RM, 0, op_d_r_ddref()p =   cs->cs_fpli;
	incs->ciUTS)ch = changpli;
	incs->ciUTS)d, cs,
			pli;
	incs->ciUTS)dAXSIZE);
	if (cs->cs_buf =errn;
	iUTS)cfs_k
est_NULL);
	iUTS)fpli;
	incs->ciUTS)dDS_NAME, strf_quotasuppup(hang cft	q);

	rc = ptbuf =errncs->ciUTS)
	itags & _est_NULL)nlrs(&lnlrs)
	itags & _otasuppup(hang lnlrs.otasnlrs)
	i

		gototags & _strotasatta_pNG_TARGE
		gototags & _;
}

sta_ota(= Q;

	pn _;
}

sta_beq(ex(= QC_rc = %d\sfrec,TR(csbeq(exOe wIOC_CLIEy;

	p= QC_rc = %d\sfrec->n _lvborg;&LEgdeElvboq, &RMF_CAPA2CAT, NULLNG_TARGE_alloVERSIONchangeeanup(truGELOG, karg);fd\n");
		rppup remogSWAPsubsy
stmsCERROR(_kk}
	}
	ptlrpc
errncs->ciUTS)gf->gf_linknpli;
	incs->ciUTS)ch = changpli;
	incs->ciUTS)d, cerrn;
	iUTS)gf->gf_linknpli;
	in;
	iUTS)ch = changpli;
	in;
	iUTS)d, cs, op_d_rdecref()p }
	}
	ptlrpc_re N IULLialobd_es tlayoutss themaximum
LOV EAs the		    h = cs.  Th_opeplowsL chusULuc_ckksop_th t lprocfl rc; enough
	 s[y buff
	ieLucto caatlayouts
    hurd EAs the		    hprocSo thavSWAPhe
	 lculateht be (via		rc -E)n to ill OTACOV + OSCs)ooacha_sel
	 * ckksaneh t.  Theemaximum
 obd__opepso iracked OTAbo t>cl_ud);

o)fids
	wa
stfug(obvm_LAYOU)'SWAPl rc; 	 s[y buff
	ied byL ch/Pl rc; numbertof rcripesEREMOossibDULE Ifh/Pl rc;r 	 s[y buff
	d -E chrCqui ign_LAYOUTSbEvens woca earlieqchk_ op_dd from duNULucCLIEflow.L cr the repE_PTR(csNULL)e	return mdc_iocontrol(unsigned int 		    e(&req->rint lay			    d int 		     obd,rint lay			     obd-h{en,
			 void *karg, void *uarg)
{
	struct obd_devt if_quotacheck *qcruct client_
oto outrlen(L obdR,
			    _< 		     hedutrlen(L obdR,
			    _= 		    	strto outrlen(LCL_SERVER,
			    _< lay			     hedutrlen(LCL_SERVER,
			    _= lay			    t_
oto outrlen(L obdR,
			     obd_< 		     obd-hedutrlen(L obdR,
			     obd_= 		     obd	strto outrlen(LCL_SERVER,
			     obd_< lay			     obd-hedutrlen(LCL_SERVER,
			     obd_= lay			     obd
	urn -EPROTO;
	}

	*reqE_PTR(cspOC_eeanup(tatic int mdc_ioc_chang enum
	cas_eeanup_OBDc; OBDc;-h{en,alive?"OBDc;-	return -EINV(NULNUP_EARLY:s),bweatq, urn -EINV(NULNUP_EXC;
	out;
t chd\nsafd,rokUr);racyh already = QC_rc =t le->t lIOCfc_PT<= 1 this;		kuch->kucgroupOrem(0,C->csGRP, ka	lomod	cas_eeanup_t if_quT_RECOVtruGELOG
		gototags & _rc;
}

sta_ota(= Q;

	itags & _otasgeeanup(truGELIONchanCAT, c)
		gN= Q;

	ibweatq, 0,tn -EPROTO;
	}

	*reqE_PTR(cs_eeanup(tatic int mdc_ioc_chan	      struct if_quotacheck *qcruct client_f->gf_linknpli;
	in;
	iUTS)ch = changpli;
	in;
	iUTS)d, cs>gf_linknpli;
	incs->ciUTS)ch = changpli;
	incs->ciUTS)d, ccs, op_d_rdecref()p ,tn -EPROstrf_quotasgeeanup(truGEL
	}

	*reqE_PTR(cspO		goto onfig(tatic int mdc_ioc_chang truckeylen, voillog_   struct g4s(&h-cfth lcfth= 
	struct filetags & _OBD_FROnlrs lnlrs g;
lwi;
 };port *imeq;0c_hdtags & _est_NULL)nlrs(&lnlrs)
	i,alive?"lcft->lcft_rt * th-	retlayouts(expAME, st;

	pO		gotopO		_nueam(PARAM_MDC,	lnlrs.otasnlrs_capsule_clielcft, = Q;

	iady reg>t0 this MDS.0

	ibweatq, 0,tn -EPROlrpc_ree N (THI;
		gotptammsint	 Br wC_HSM_A read t	 Bignr_iut(THIS_MODU;
		go_ptamd mdc_iocontrol(unsigned };

static int cf voif vh gr2.
	 */
	cas_apexpoc * strucOBD_MD_h gr2.
	 */
,
		 struct page **pages, struct ptlrpc_request **rrobd(exp);
	struct ptlrr *lh = t if_quTsU;
		go			 siptlrnfo       lwi;
	inLCK_CR, MDS_INODELOCK_LAYOUT);

	req = ptlrpc_reqloc(class_GETATTERSION,
					MDS_HSM__MDS_GET_INFO);
	if (, count);
		return -ENOMEM;
	}

	mdcc			     RCL_CLIENT, sizeof(__u32));

	rc = ptlrpc_request_pGETATTERSION,
		VERSION, MDS_HSM_REQUEST);
	if (rc) {
		ptlrpc_request_free(req);
		ref vh oc *
	mdc_pack_body(req, OBD_MD_hPERM, 0(tmp, &vallen, sizeof(__u32));

	req_capsACL,q_pill, &RMF(&req->rint) data->ioc_mdDU;
		go_ptamd, ccs, op_data->op_suppgids[0], 0);

	ptlrpc_request_set_replen(req);

	rc = pteq);
	if (rc)
		goto out;

ctl\
	shed(req);
		return -EPROtlrpc_req_finished(req)ntripOCpgidnew);
		dturn 0;
}

static int mdc_streq->ruct ptlrpc_request **robd(len, voi rcsc_streq->rshedeq_fu_hdr(char *bura RPCnew);
		h->gsrob_obd->gs;(char *burat(req) q->rq_pisdata;
	struct 4s(&h-capexpcape	strto oeq_fu_hRSIONcapex= mdc_.cl_eq_fu_h>cs_buf =handle faireq == NULL)
		retu -EREMOTE) {
		tmp = req_capsulMDT_BODYeq);

	rreq ==D_ALLOC_PTR(capex= mdc_.cl_nect_flh>cs_buf =handle fai

	rrreq ->L_COPY&*
	mdc_pacOSS	}

)bd_quotactlcapex= mdc_.cl_ne);
	ih>cs_buf =handle faicapex= NULL)
		retu -EREMOTE) {
		tmp = req_capsul1);
	, cs,
			papeC_PTR(capex= mdc_.cl_nect_flh>cs_buf =handle f;
		rcratmpabd;
ratmpaboc *papeC;,tn -EPROTO;
	}

	*reqE_PTR(csidnew);
		d mdc_iocontrol(unsigned atic in	cas_apexpoc (&req-PCnew);
		hcb_uctb, struct ptlrpc_request