/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Client-side XDR for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>

#include "nfs4_fs.h"
#include "internal.h"
#include "nfs4session.h"
#include "pnfs.h"
#include "netns.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

static int nfs4_stat_to_errno(int);

/* NFSv4 COMPOUND tags are only wanted for debugging purposes */
#ifdef DEBUG
#define NFS4_MAXTAGLEN		20
#else
#define NFS4_MAXTAGLEN		0
#endif

/* lock,open owner id:
 * we currently use size 2 (u64) out of (NFS4_OPAQUE_LIMIT  >> 2)
 */
#define open_owner_id_maxsz	(1 + 2 + 1 + 1 + 2)
#define lock_owner_id_maxsz	(1 + 1 + 4)
#define decode_lockowner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define compound_encode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define compound_decode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define op_encode_hdr_maxsz	(1)
#define op_decode_hdr_maxsz	(2)
#define encode_stateid_maxsz	(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_stateid_maxsz	(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define encode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define decode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define encode_putfh_maxsz	(op_encode_hdr_maxsz + 1 + \
				(NFS4_FHSIZE >> 2))
#define decode_putfh_maxsz	(op_decode_hdr_maxsz)
#define encode_putrootfh_maxsz	(op_encode_hdr_maxsz)
#define decode_putrootfh_maxsz	(op_decode_hdr_maxsz)
#define encode_getfh_maxsz      (op_encode_hdr_maxsz)
#define decode_getfh_maxsz      (op_decode_hdr_maxsz + 1 + \
				((3+NFS4_FHSIZE) >> 2))
#define nfs4_fattr_bitmap_maxsz 4
#define encode_getattr_maxsz    (op_encode_hdr_maxsz + nfs4_fattr_bitmap_maxsz)
#define nfs4_name_maxsz		(1 + ((3 + NFS4_MAXNAMLEN) >> 2))
#define nfs4_path_maxsz		(1 + ((3 + NFS4_MAXPATHLEN) >> 2))
#define nfs4_owner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#define nfs4_group_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
/* PI(4 bytes) + LFS(4 bytes) + 1(for null terminator?) + MAXLABELLEN */
#define	nfs4_label_maxsz	(4 + 4 + 1 + XDR_QUADLEN(NFS4_MAXLABELLEN))
#else
#define	nfs4_label_maxsz	0
#endif
/* We support only one layout type per file system */
#define decode_mdsthreshold_maxsz (1 + 1 + nfs4_fattr_bitmap_maxsz + 1 + 8)
/* This is based on getfattr, which uses the most attributes: */
#define nfs4_fattr_value_maxsz	(1 + (1 + 2 + 2 + 4 + 2 + 1 + 1 + 2 + 2 + \
				3 + 3 + 3 + nfs4_owner_maxsz + \
				nfs4_group_maxsz + nfs4_label_maxsz + \
				 decode_mdsthreshold_maxsz))
#define nfs4_fattr_maxsz	(nfs4_fattr_bitmap_maxsz + \
				nfs4_fattr_value_maxsz)
#define decode_getattr_maxsz    (op_decode_hdr_maxsz + nfs4_fattr_maxsz)
#define encode_attrs_maxsz	(nfs4_fattr_bitmap_maxsz + \
				 1 + 2 + 1 + \
				nfs4_owner_maxsz + \
				nfs4_group_maxsz + \
				nfs4_label_maxsz + \
				4 + 4)
#define encode_savefh_maxsz     (op_encode_hdr_maxsz)
#define decode_savefh_maxsz     (op_decode_hdr_maxsz)
#define encode_restorefh_maxsz  (op_encode_hdr_maxsz)
#define decode_restorefh_maxsz  (op_decode_hdr_maxsz)
#define encode_fsinfo_maxsz	(encode_getattr_maxsz)
/* The 5 accounts for the PNFS attributes, and assumes that at most three
 * layout types will be returned.
 */
#define decode_fsinfo_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz + 4 + 8 + 5)
#define encode_renew_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_renew_maxsz	(op_decode_hdr_maxsz)
#define encode_setclientid_maxsz \
				(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_VERIFIER_SIZE) + \
				XDR_QUADLEN(NFS4_SETCLIENTID_NAMELEN) + \
				1 /* sc_prog */ + \
				XDR_QUADLEN(RPCBIND_MAXNETIDLEN) + \
				XDR_QUADLEN(RPCBIND_MAXUADDRLEN) + \
				1) /* sc_cb_ident */
#define decode_setclientid_maxsz \
				(op_decode_hdr_maxsz + \
				2 + \
				1024) /* large value for CLID_INUSE */
#define encode_setclientid_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				3 + (NFS4_VERIFIER_SIZE >> 2))
#define decode_setclientid_confirm_maxsz \
				(op_decode_hdr_maxsz)
#define encode_lookup_maxsz	(op_encode_hdr_maxsz + nfs4_name_maxsz)
#define decode_lookup_maxsz	(op_decode_hdr_maxsz)
#define encode_share_access_maxsz \
				(2)
#define encode_createmode_maxsz	(1 + encode_attrs_maxsz + encode_verifier_maxsz)
#define encode_opentype_maxsz	(1 + encode_createmode_maxsz)
#define encode_claim_null_maxsz	(1 + nfs4_name_maxsz)
#define encode_open_maxsz	(op_encode_hdr_maxsz + \
				2 + encode_share_access_maxsz + 2 + \
				open_owner_id_maxsz + \
				encode_opentype_maxsz + \
				encode_claim_null_maxsz)
#define decode_ace_maxsz	(3 + nfs4_owner_maxsz)
#define decode_delegation_maxsz	(1 + decode_stateid_maxsz + 1 + \
				decode_ace_maxsz)
#define decode_change_info_maxsz	(5)
#define decode_open_maxsz	(op_decode_hdr_maxsz + \
				decode_stateid_maxsz + \
				decode_change_info_maxsz + 1 + \
				nfs4_fattr_bitmap_maxsz + \
				decode_delegation_maxsz)
#define encode_open_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 1)
#define decode_open_confirm_maxsz \
				(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_open_downgrade_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 1 + \
				 encode_share_access_maxsz)
#define decode_open_downgrade_maxsz \
				(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_close_maxsz	(op_encode_hdr_maxsz + \
				 1 + encode_stateid_maxsz)
#define decode_close_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_setattr_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + \
				 encode_attrs_maxsz)
#define decode_setattr_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz)
#define encode_read_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_read_maxsz	(op_decode_hdr_maxsz + 2)
#define encode_readdir_maxsz	(op_encode_hdr_maxsz + \
				 2 + encode_verifier_maxsz + 5 + \
				nfs4_label_maxsz)
#define decode_readdir_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz)
#define encode_readlink_maxsz	(op_encode_hdr_maxsz)
#define decode_readlink_maxsz	(op_decode_hdr_maxsz + 1)
#define encode_write_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 4)
#define decode_write_maxsz	(op_decode_hdr_maxsz + \
				 2 + decode_verifier_maxsz)
#define encode_commit_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_commit_maxsz	(op_decode_hdr_maxsz + \
				 decode_verifier_maxsz)
#define encode_remove_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define decode_remove_maxsz	(op_decode_hdr_maxsz + \
				 decode_change_info_maxsz)
#define encode_rename_maxsz	(op_encode_hdr_maxsz + \
				2 * nfs4_name_maxsz)
#define decode_rename_maxsz	(op_decode_hdr_maxsz + \
				 decode_change_info_maxsz + \
				 decode_change_info_maxsz)
#define encode_link_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define decode_link_maxsz	(op_decode_hdr_maxsz + decode_change_info_maxsz)
#define encode_lockowner_maxsz	(7)
#define encode_lock_maxsz	(op_encode_hdr_maxsz + \
				 7 + \
				 1 + encode_stateid_maxsz + 1 + \
				 encode_lockowner_maxsz)
#define decode_lock_denied_maxsz \
				(8 + decode_lockowner_maxsz)
#define decode_lock_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_lockt_maxsz	(op_encode_hdr_maxsz + 5 + \
				encode_lockowner_maxsz)
#define decode_lockt_maxsz	(op_decode_hdr_maxsz + \
				 decode_lock_denied_maxsz)
#define encode_locku_maxsz	(op_encode_hdr_maxsz + 3 + \
				 encode_stateid_maxsz + \
				 4)
#define decode_locku_maxsz	(op_decode_hdr_maxsz + \
				 decode_stateid_maxsz)
#define encode_release_lockowner_maxsz \
				(op_encode_hdr_maxsz + \
				 encode_lockowner_maxsz)
#define decode_release_lockowner_maxsz \
				(op_decode_hdr_maxsz)
#define encode_access_maxsz	(op_encode_hdr_maxsz + 1)
#define decode_access_maxsz	(op_decode_hdr_maxsz + 2)
#define encode_symlink_maxsz	(op_encode_hdr_maxsz + \
				1 + nfs4_name_maxsz + \
				1 + \
				nfs4_fattr_maxsz)
#define decode_symlink_maxsz	(op_decode_hdr_maxsz + 8)
#define encode_create_maxsz	(op_encode_hdr_maxsz + \
				1 + 2 + nfs4_name_maxsz + \
				encode_attrs_maxsz)
#define decode_create_maxsz	(op_decode_hdr_maxsz + \
				decode_change_info_maxsz + \
				nfs4_fattr_bitmap_maxsz)
#define encode_statfs_maxsz	(encode_getattr_maxsz)
#define decode_statfs_maxsz	(decode_getattr_maxsz)
#define encode_delegreturn_maxsz (op_encode_hdr_maxsz + 4)
#define decode_delegreturn_maxsz (op_decode_hdr_maxsz)
#define encode_getacl_maxsz	(encode_getattr_maxsz)
#define decode_getacl_maxsz	(op_decode_hdr_maxsz + \
				 nfs4_fattr_bitmap_maxsz + 1)
#define encode_setacl_maxsz	(op_encode_hdr_maxsz + \
				 encode_stateid_maxsz + 3)
#define decode_setacl_maxsz	(decode_setattr_maxsz)
#define encode_fs_locations_maxsz \
				(encode_getattr_maxsz)
#define decode_fs_locations_maxsz \
				(0)
#define encode_secinfo_maxsz	(op_encode_hdr_maxsz + nfs4_name_maxsz)
#define decode_secinfo_maxsz	(op_decode_hdr_maxsz + 1 + ((NFS_MAX_SECFLAVORS * (16 + GSS_OID_MAX_LEN)) / 4))

#if defined(CONFIG_NFS_V4_1)
#define NFS4_MAX_MACHINE_NAME_LEN (64)
#define IMPL_NAME_LIMIT (sizeof(utsname()->sysname) + sizeof(utsname()->release) + \
			 sizeof(utsname()->version) + sizeof(utsname()->machine) + 8)

#define encode_exchange_id_maxsz (op_encode_hdr_maxsz + \
				encode_verifier_maxsz + \
				1 /* co_ownerid.len */ + \
				XDR_QUADLEN(NFS4_EXCHANGE_ID_LEN) + \
				1 /* flags */ + \
				1 /* spa_how */ + \
				/* max is SP4_MACH_CRED (for now) */ + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				1 /* implementation id array of size 1 */ + \
				1 /* nii_domain */ + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				1 /* nii_name */ + \
				XDR_QUADLEN(IMPL_NAME_LIMIT) + \
				3 /* nii_date */)
#define decode_exchange_id_maxsz (op_decode_hdr_maxsz + \
				2 /* eir_clientid */ + \
				1 /* eir_sequenceid */ + \
				1 /* eir_flags */ + \
				1 /* spr_how */ + \
				  /* max is SP4_MACH_CRED (for now) */ + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				1 + NFS4_OP_MAP_NUM_WORDS + \
				2 /* eir_server_owner.so_minor_id */ + \
				/* eir_server_owner.so_major_id<> */ \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 + \
				/* eir_server_scope<> */ \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 + \
				1 /* eir_server_impl_id array length */ + \
				1 /* nii_domain */ + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				1 /* nii_name */ + \
				XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + \
				3 /* nii_date */)
#define encode_channel_attrs_maxsz  (6 + 1 /* ca_rdma_ird.len (0) */)
#define decode_channel_attrs_maxsz  (6 + \
				     1 /* ca_rdma_ird.len */ + \
				     1 /* ca_rdma_ird */)
#define encode_create_session_maxsz  (op_encode_hdr_maxsz + \
				     2 /* csa_clientid */ + \
				     1 /* csa_sequence */ + \
				     1 /* csa_flags */ + \
				     encode_channel_attrs_maxsz + \
				     encode_channel_attrs_maxsz + \
				     1 /* csa_cb_program */ + \
				     1 /* csa_sec_parms.len (1) */ + \
				     1 /* cb_secflavor (AUTH_SYS) */ + \
				     1 /* stamp */ + \
				     1 /* machinename.len */ + \
				     XDR_QUADLEN(NFS4_MAX_MACHINE_NAME_LEN) + \
				     1 /* uid */ + \
				     1 /* gid */ + \
				     1 /* gids.len (0) */)
#define decode_create_session_maxsz  (op_decode_hdr_maxsz +	\
				     XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				     1 /* csr_sequence */ + \
				     1 /* csr_flags */ + \
				     decode_channel_attrs_maxsz + \
				     decode_channel_attrs_maxsz)
#define encode_bind_conn_to_session_maxsz  (op_encode_hdr_maxsz + \
				     /* bctsa_sessid */ \
				     XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				     1 /* bctsa_dir */ + \
				     1 /* bctsa_use_conn_in_rdma_mode */)
#define decode_bind_conn_to_session_maxsz  (op_decode_hdr_maxsz +	\
				     /* bctsr_sessid */ \
				     XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				     1 /* bctsr_dir */ + \
				     1 /* bctsr_use_conn_in_rdma_mode */)
#define encode_destroy_session_maxsz    (op_encode_hdr_maxsz + 4)
#define decode_destroy_session_maxsz    (op_decode_hdr_maxsz)
#define encode_destroy_clientid_maxsz   (op_encode_hdr_maxsz + 2)
#define decode_destroy_clientid_maxsz   (op_decode_hdr_maxsz)
#define encode_sequence_maxsz	(op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 4)
#define decode_sequence_maxsz	(op_decode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 5)
#define encode_reclaim_complete_maxsz	(op_encode_hdr_maxsz + 4)
#define decode_reclaim_complete_maxsz	(op_decode_hdr_maxsz + 4)
#define encode_getdeviceinfo_maxsz (op_encode_hdr_maxsz + \
				XDR_QUADLEN(NFS4_DEVICEID4_SIZE) + \
				1 /* layout type */ + \
				1 /* maxcount */ + \
				1 /* bitmap size */ + \
				1 /* notification bitmap length */ + \
				1 /* notification bitmap, word 0 */)
#define decode_getdeviceinfo_maxsz (op_decode_hdr_maxsz + \
				1 /* layout type */ + \
				1 /* opaque devaddr4 length */ + \
				  /* devaddr4 payload is read into page */ \
				1 /* notification bitmap length */ + \
				1 /* notification bitmap, word 0 */)
#define encode_layoutget_maxsz	(op_encode_hdr_maxsz + 10 + \
				encode_stateid_maxsz)
#define decode_layoutget_maxsz	(op_decode_hdr_maxsz + 8 + \
				decode_stateid_maxsz + \
				XDR_QUADLEN(PNFS_LAYOUT_MAXSIZE))
#define encode_layoutcommit_maxsz (op_encode_hdr_maxsz +          \
				2 /* offset */ + \
				2 /* length */ + \
				1 /* reclaim */ + \
				encode_stateid_maxsz + \
				1 /* new offset (true) */ + \
				2 /* last byte written */ + \
				1 /* nt_timechanged (false) */ + \
				1 /* layoutupdate4 layout type */ + \
				1 /* layoutupdate4 opaqueue len */)
				  /* the actual content of layoutupdate4 should
				     be allocated by drivers and spliced in
				     using xdr_write_pages */
#define decode_layoutcommit_maxsz (op_decode_hdr_maxsz + 3)
#define encode_layoutreturn_maxsz (8 + op_encode_hdr_maxsz + \
				encode_stateid_maxsz + \
				1 /* FIXME: opaque lrf_body always empty at the moment */)
#define decode_layoutreturn_maxsz (op_decode_hdr_maxsz + \
				1 + decode_stateid_maxsz)
#define encode_secinfo_no_name_maxsz (op_encode_hdr_maxsz + 1)
#define decode_secinfo_no_name_maxsz decode_secinfo_maxsz
#define encode_test_stateid_maxsz	(op_encode_hdr_maxsz + 2 + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_test_stateid_maxsz	(op_decode_hdr_maxsz + 2 + 1)
#define encode_free_stateid_maxsz	(op_encode_hdr_maxsz + 1 + \
					 XDR_QUADLEN(NFS4_STATEID_SIZE))
#define decode_free_stateid_maxsz	(op_decode_hdr_maxsz)
#else /* CONFIG_NFS_V4_1 */
#define encode_sequence_maxsz	0
#define decode_sequence_maxsz	0
#endif /* CONFIG_NFS_V4_1 */

#define NFS4_enc_compound_sz	(1024)  /* XXX: large enough? */
#define NFS4_dec_compound_sz	(1024)  /* XXX: large enough? */
#define NFS4_enc_read_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_read_maxsz)
#define NFS4_dec_read_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_read_maxsz)
#define NFS4_enc_readlink_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_readlink_maxsz)
#define NFS4_dec_readlink_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_readlink_maxsz)
#define NFS4_enc_readdir_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_readdir_maxsz)
#define NFS4_dec_readdir_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_readdir_maxsz)
#define NFS4_enc_write_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_write_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_write_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_write_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_commit_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_commit_maxsz)
#define NFS4_dec_commit_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_commit_maxsz)
#define NFS4_enc_open_sz        (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_open_maxsz + \
				encode_access_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_open_sz        (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_open_maxsz + \
				decode_access_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_open_confirm_sz \
				(compound_encode_hdr_maxsz + \
				 encode_putfh_maxsz + \
				 encode_open_confirm_maxsz)
#define NFS4_dec_open_confirm_sz \
				(compound_decode_hdr_maxsz + \
				 decode_putfh_maxsz + \
				 decode_open_confirm_maxsz)
#define NFS4_enc_open_noattr_sz	(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz + \
					encode_putfh_maxsz + \
					encode_open_maxsz + \
					encode_access_maxsz + \
					encode_getattr_maxsz)
#define NFS4_dec_open_noattr_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putfh_maxsz + \
					decode_open_maxsz + \
					decode_access_maxsz + \
					decode_getattr_maxsz)
#define NFS4_enc_open_downgrade_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_open_downgrade_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_open_downgrade_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_open_downgrade_maxsz + \
				 decode_getattr_maxsz)
#define NFS4_enc_close_sz	(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_close_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_close_sz	(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_close_maxsz + \
				 decode_getattr_maxsz)
#define NFS4_enc_setattr_sz	(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_setattr_maxsz + \
				 encode_getattr_maxsz)
#define NFS4_dec_setattr_sz	(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_setattr_maxsz + \
				 decode_getattr_maxsz)
#define NFS4_enc_fsinfo_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS4_dec_fsinfo_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_fsinfo_maxsz)
#define NFS4_enc_renew_sz	(compound_encode_hdr_maxsz + \
				encode_renew_maxsz)
#define NFS4_dec_renew_sz	(compound_decode_hdr_maxsz + \
				decode_renew_maxsz)
#define NFS4_enc_setclientid_sz	(compound_encode_hdr_maxsz + \
				encode_setclientid_maxsz)
#define NFS4_dec_setclientid_sz	(compound_decode_hdr_maxsz + \
				decode_setclientid_maxsz)
#define NFS4_enc_setclientid_confirm_sz \
				(compound_encode_hdr_maxsz + \
				encode_setclientid_confirm_maxsz)
#define NFS4_dec_setclientid_confirm_sz \
				(compound_decode_hdr_maxsz + \
				decode_setclientid_confirm_maxsz)
#define NFS4_enc_lock_sz        (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lock_maxsz)
#define NFS4_dec_lock_sz        (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_lock_maxsz)
#define NFS4_enc_lockt_sz       (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lockt_maxsz)
#define NFS4_dec_lockt_sz       (compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lockt_maxsz)
#define NFS4_enc_locku_sz       (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_locku_maxsz)
#define NFS4_dec_locku_sz       (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_locku_maxsz)
#define NFS4_enc_release_lockowner_sz \
				(compound_encode_hdr_maxsz + \
				 encode_lockowner_maxsz)
#define NFS4_dec_release_lockowner_sz \
				(compound_decode_hdr_maxsz + \
				 decode_lockowner_maxsz)
#define NFS4_enc_access_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_access_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_access_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_access_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_getattr_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz + \
				encode_renew_maxsz)
#define NFS4_dec_getattr_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz + \
				decode_renew_maxsz)
#define NFS4_enc_lookup_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_lookup_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookup_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_lookup_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_lookup_root_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putrootfh_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookup_root_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putrootfh_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_remove_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_remove_maxsz)
#define NFS4_dec_remove_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_remove_maxsz)
#define NFS4_enc_rename_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_rename_maxsz)
#define NFS4_dec_rename_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_rename_maxsz)
#define NFS4_enc_link_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_link_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_link_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_link_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_symlink_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_symlink_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_symlink_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_symlink_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_create_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_create_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_create_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_create_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_pathconf_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_pathconf_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_statfs_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_statfs_maxsz)
#define NFS4_dec_statfs_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_statfs_maxsz)
#define NFS4_enc_server_caps_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_server_caps_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_delegreturn_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_delegreturn_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_delegreturn_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_delegreturn_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_getacl_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_getacl_maxsz)
#define NFS4_dec_getacl_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_getacl_maxsz)
#define NFS4_enc_setacl_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_setacl_maxsz)
#define NFS4_dec_setacl_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_setacl_maxsz)
#define NFS4_enc_fs_locations_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_lookup_maxsz + \
				 encode_fs_locations_maxsz + \
				 encode_renew_maxsz)
#define NFS4_dec_fs_locations_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_lookup_maxsz + \
				 decode_fs_locations_maxsz + \
				 decode_renew_maxsz)
#define NFS4_enc_secinfo_sz 	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_secinfo_maxsz)
#define NFS4_dec_secinfo_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_secinfo_maxsz)
#define NFS4_enc_fsid_present_sz \
				(compound_encode_hdr_maxsz + \
				 encode_sequence_maxsz + \
				 encode_putfh_maxsz + \
				 encode_getfh_maxsz + \
				 encode_renew_maxsz)
#define NFS4_dec_fsid_present_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz + \
				 decode_putfh_maxsz + \
				 decode_getfh_maxsz + \
				 decode_renew_maxsz)
#if defined(CONFIG_NFS_V4_1)
#define NFS4_enc_bind_conn_to_session_sz \
				(compound_encode_hdr_maxsz + \
				 encode_bind_conn_to_session_maxsz)
#define NFS4_dec_bind_conn_to_session_sz \
				(compound_decode_hdr_maxsz + \
				 decode_bind_conn_to_session_maxsz)
#define NFS4_enc_exchange_id_sz \
				(compound_encode_hdr_maxsz + \
				 encode_exchange_id_maxsz)
#define NFS4_dec_exchange_id_sz \
				(compound_decode_hdr_maxsz + \
				 decode_exchange_id_maxsz)
#define NFS4_enc_create_session_sz \
				(compound_encode_hdr_maxsz + \
				 encode_create_session_maxsz)
#define NFS4_dec_create_session_sz \
				(compound_decode_hdr_maxsz + \
				 decode_create_session_maxsz)
#define NFS4_enc_destroy_session_sz	(compound_encode_hdr_maxsz + \
					 encode_destroy_session_maxsz)
#define NFS4_dec_destroy_session_sz	(compound_decode_hdr_maxsz + \
					 decode_destroy_session_maxsz)
#define NFS4_enc_destroy_clientid_sz	(compound_encode_hdr_maxsz + \
					 encode_destroy_clientid_maxsz)
#define NFS4_dec_destroy_clientid_sz	(compound_decode_hdr_maxsz + \
					 decode_destroy_clientid_maxsz)
#define NFS4_enc_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 encode_sequence_maxsz)
#define NFS4_dec_sequence_sz \
				(compound_decode_hdr_maxsz + \
				 decode_sequence_maxsz)
#define NFS4_enc_get_lease_time_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_putrootfh_maxsz + \
					 encode_fsinfo_maxsz)
#define NFS4_dec_get_lease_time_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_putrootfh_maxsz + \
					 decode_fsinfo_maxsz)
#define NFS4_enc_reclaim_complete_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_reclaim_complete_maxsz)
#define NFS4_dec_reclaim_complete_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_reclaim_complete_maxsz)
#define NFS4_enc_getdeviceinfo_sz (compound_encode_hdr_maxsz +    \
				encode_sequence_maxsz +\
				encode_getdeviceinfo_maxsz)
#define NFS4_dec_getdeviceinfo_sz (compound_decode_hdr_maxsz +    \
				decode_sequence_maxsz + \
				decode_getdeviceinfo_maxsz)
#define NFS4_enc_layoutget_sz	(compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz +        \
				encode_layoutget_maxsz)
#define NFS4_dec_layoutget_sz	(compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz +        \
				decode_layoutget_maxsz)
#define NFS4_enc_layoutcommit_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz +\
				encode_putfh_maxsz + \
				encode_layoutcommit_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_layoutcommit_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_layoutcommit_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_layoutreturn_sz (compound_encode_hdr_maxsz + \
				encode_sequence_maxsz + \
				encode_putfh_maxsz + \
				encode_layoutreturn_maxsz)
#define NFS4_dec_layoutreturn_sz (compound_decode_hdr_maxsz + \
				decode_sequence_maxsz + \
				decode_putfh_maxsz + \
				decode_layoutreturn_maxsz)
#define NFS4_enc_secinfo_no_name_sz	(compound_encode_hdr_maxsz + \
					encode_sequence_maxsz + \
					encode_putrootfh_maxsz +\
					encode_secinfo_no_name_maxsz)
#define NFS4_dec_secinfo_no_name_sz	(compound_decode_hdr_maxsz + \
					decode_sequence_maxsz + \
					decode_putrootfh_maxsz + \
					decode_secinfo_no_name_maxsz)
#define NFS4_enc_test_stateid_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_test_stateid_maxsz)
#define NFS4_dec_test_stateid_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_test_stateid_maxsz)
#define NFS4_enc_free_stateid_sz	(compound_encode_hdr_maxsz + \
					 encode_sequence_maxsz + \
					 encode_free_stateid_maxsz)
#define NFS4_dec_free_stateid_sz	(compound_decode_hdr_maxsz + \
					 decode_sequence_maxsz + \
					 decode_free_stateid_maxsz)

const u32 nfs41_maxwrite_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
				      compound_encode_hdr_maxsz +
				      encode_sequence_maxsz +
				      encode_putfh_maxsz +
				      encode_getattr_maxsz) *
				     XDR_UNIT);

const u32 nfs41_maxread_overhead = ((RPC_MAX_HEADER_WITH_AUTH +
				     compound_decode_hdr_maxsz +
				     decode_sequence_maxsz +
				     decode_putfh_maxsz) *
				    XDR_UNIT);

const u32 nfs41_maxgetdevinfo_overhead = ((RPC_MAX_REPHEADER_WITH_AUTH +
					   compound_decode_hdr_maxsz +
					   decode_sequence_maxsz) *
					  XDR_UNIT);
EXPORT_SYMBOL_GPL(nfs41_maxgetdevinfo_overhead);
#endif /* CONFIG_NFS_V4_1 */

static const umode_t nfs_type2fmt[] = {
	[NF4BAD] = 0,
	[NF4REG] = S_IFREG,
	[NF4DIR] = S_IFDIR,
	[NF4BLK] = S_IFBLK,
	[NF4CHR] = S_IFCHR,
	[NF4LNK] = S_IFLNK,
	[NF4SOCK] = S_IFSOCK,
	[NF4FIFO] = S_IFIFO,
	[NF4ATTRDIR] = 0,
	[NF4NAMEDATTR] = 0,
};

struct compound_hdr {
	int32_t		status;
	uint32_t	nops;
	__be32 *	nops_p;
	uint32_t	taglen;
	char *		tag;
	uint32_t	replen;		/* expected reply words */
	u32		minorversion;
};

static __be32 *reserve_space(struct xdr_stream *xdr, size_t nbytes)
{
	__be32 *p = xdr_reserve_space(xdr, nbytes);
	BUG_ON(!p);
	return p;
}

static void encode_opaque_fixed(struct xdr_stream *xdr, const void *buf, size_t len)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, len);
	xdr_encode_opaque_fixed(p, buf, len);
}

static void encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	__be32 *p;

	p = reserve_space(xdr, 4 + len);
	xdr_encode_opaque(p, str, len);
}

static void encode_uint32(struct xdr_stream *xdr, u32 n)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(n);
}

static void encode_uint64(struct xdr_stream *xdr, u64 n)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	xdr_encode_hyper(p, n);
}

static void encode_nfs4_seqid(struct xdr_stream *xdr,
		const struct nfs_seqid *seqid)
{
	encode_uint32(xdr, seqid->sequence->counter);
}

static void encode_compound_hdr(struct xdr_stream *xdr,
				struct rpc_rqst *req,
				struct compound_hdr *hdr)
{
	__be32 *p;
	struct rpc_auth *auth = req->rq_cred->cr_auth;

	/* initialize running count of expected bytes in reply.
	 * NOTE: the replied tag SHOULD be the same is the one sent,
	 * but this is not required as a MUST for the server to do so. */
	hdr->replen = RPC_REPHDRSIZE + auth->au_rslack + 3 + hdr->taglen;

	WARN_ON_ONCE(hdr->taglen > NFS4_MAXTAGLEN);
	encode_string(xdr, hdr->taglen, hdr->tag);
	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(hdr->minorversion);
	hdr->nops_p = p;
	*p = cpu_to_be32(hdr->nops);
}

static void encode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 op,
		uint32_t replen,
		struct compound_hdr *hdr)
{
	encode_uint32(xdr, op);
	hdr->nops++;
	hdr->replen += replen;
}

static void encode_nops(struct compound_hdr *hdr)
{
	WARN_ON_ONCE(hdr->nops > NFS4_MAX_OPS);
	*hdr->nops_p = htonl(hdr->nops);
}

static void encode_nfs4_stateid(struct xdr_stream *xdr, const nfs4_stateid *stateid)
{
	encode_opaque_fixed(xdr, stateid, NFS4_STATEID_SIZE);
}

static void encode_nfs4_verifier(struct xdr_stream *xdr, const nfs4_verifier *verf)
{
	encode_opaque_fixed(xdr, verf->data, NFS4_VERIFIER_SIZE);
}

static void encode_attrs(struct xdr_stream *xdr, const struct iattr *iap,
				const struct nfs4_label *label,
				const struct nfs_server *server)
{
	char owner_name[IDMAP_NAMESZ];
	char owner_group[IDMAP_NAMESZ];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	__be32 *p;
	unsigned i;
	uint32_t len = 0;
	uint32_t bmval_len;
	uint32_t bmval[3] = { 0 };

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 * In the worst-case, this would be
	 * 16(bitmap) + 4(attrlen) + 8(size) + 4(mode) + 4(atime) + 4(mtime)
	 * = 40 bytes, plus any contribution from variable-length fields
	 *            such as owner/group.
	 */
	if (iap->ia_valid & ATTR_SIZE) {
		bmval[0] |= FATTR4_WORD0_SIZE;
		len += 8;
	}
	if (iap->ia_valid & ATTR_MODE) {
		bmval[1] |= FATTR4_WORD1_MODE;
		len += 4;
	}
	if (iap->ia_valid & ATTR_UID) {
		owner_namelen = nfs_map_uid_to_name(server, iap->ia_uid, owner_name, IDMAP_NAMESZ);
		if (owner_namelen < 0) {
			dprintk("nfs: couldn't resolve uid %d to string\n",
					from_kuid(&init_user_ns, iap->ia_uid));
			/* XXX */
			strcpy(owner_name, "nobody");
			owner_namelen = sizeof("nobody") - 1;
			/* goto out; */
		}
		bmval[1] |= FATTR4_WORD1_OWNER;
		len += 4 + (XDR_QUADLEN(owner_namelen) << 2);
	}
	if (iap->ia_valid & ATTR_GID) {
		owner_grouplen = nfs_map_gid_to_group(server, iap->ia_gid, owner_group, IDMAP_NAMESZ);
		if (owner_grouplen < 0) {
			dprintk("nfs: couldn't resolve gid %d to string\n",
					from_kgid(&init_user_ns, iap->ia_gid));
			strcpy(owner_group, "nobody");
			owner_grouplen = sizeof("nobody") - 1;
			/* goto out; */
		}
		bmval[1] |= FATTR4_WORD1_OWNER_GROUP;
		len += 4 + (XDR_QUADLEN(owner_grouplen) << 2);
	}
	if (iap->ia_valid & ATTR_ATIME_SET) {
		bmval[1] |= FATTR4_WORD1_TIME_ACCESS_SET;
		len += 16;
	} else if (iap->ia_valid & ATTR_ATIME) {
		bmval[1] |= FATTR4_WORD1_TIME_ACCESS_SET;
		len += 4;
	}
	if (iap->ia_valid & ATTR_MTIME_SET) {
		bmval[1] |= FATTR4_WORD1_TIME_MODIFY_SET;
		len += 16;
	} else if (iap->ia_valid & ATTR_MTIME) {
		bmval[1] |= FATTR4_WORD1_TIME_MODIFY_SET;
		len += 4;
	}
	if (label) {
		len += 4 + 4 + 4 + (XDR_QUADLEN(label->len) << 2);
		bmval[2] |= FATTR4_WORD2_SECURITY_LABEL;
	}

	if (bmval[2] != 0)
		bmval_len = 3;
	else if (bmval[1] != 0)
		bmval_len = 2;
	else
		bmval_len = 1;

	p = reserve_space(xdr, 4 + (bmval_len << 2) + 4 + len);

	*p++ = cpu_to_be32(bmval_len);
	for (i = 0; i < bmval_len; i++)
		*p++ = cpu_to_be32(bmval[i]);
	*p++ = cpu_to_be32(len);

	if (bmval[0] & FATTR4_WORD0_SIZE)
		p = xdr_encode_hyper(p, iap->ia_size);
	if (bmval[1] & FATTR4_WORD1_MODE)
		*p++ = cpu_to_be32(iap->ia_mode & S_IALLUGO);
	if (bmval[1] & FATTR4_WORD1_OWNER)
		p = xdr_encode_opaque(p, owner_name, owner_namelen);
	if (bmval[1] & FATTR4_WORD1_OWNER_GROUP)
		p = xdr_encode_opaque(p, owner_group, owner_grouplen);
	if (bmval[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		if (iap->ia_valid & ATTR_ATIME_SET) {
			*p++ = cpu_to_be32(NFS4_SET_TO_CLIENT_TIME);
			p = xdr_encode_hyper(p, (s64)iap->ia_atime.tv_sec);
			*p++ = cpu_to_be32(iap->ia_atime.tv_nsec);
		} else
			*p++ = cpu_to_be32(NFS4_SET_TO_SERVER_TIME);
	}
	if (bmval[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		if (iap->ia_valid & ATTR_MTIME_SET) {
			*p++ = cpu_to_be32(NFS4_SET_TO_CLIENT_TIME);
			p = xdr_encode_hyper(p, (s64)iap->ia_mtime.tv_sec);
			*p++ = cpu_to_be32(iap->ia_mtime.tv_nsec);
		} else
			*p++ = cpu_to_be32(NFS4_SET_TO_SERVER_TIME);
	}
	if (bmval[2] & FATTR4_WORD2_SECURITY_LABEL) {
		*p++ = cpu_to_be32(label->lfs);
		*p++ = cpu_to_be32(label->pi);
		*p++ = cpu_to_be32(label->len);
		p = xdr_encode_opaque_fixed(p, label->label, label->len);
	}

/* out: */
}

static void encode_access(struct xdr_stream *xdr, u32 access, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_ACCESS, decode_access_maxsz, hdr);
	encode_uint32(xdr, access);
}

static void encode_close(struct xdr_stream *xdr, const struct nfs_closeargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_CLOSE, decode_close_maxsz, hdr);
	encode_nfs4_seqid(xdr, arg->seqid);
	encode_nfs4_stateid(xdr, arg->stateid);
}

static void encode_commit(struct xdr_stream *xdr, const struct nfs_commitargs *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_COMMIT, decode_commit_maxsz, hdr);
	p = reserve_space(xdr, 12);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static void encode_create(struct xdr_stream *xdr, const struct nfs4_create_arg *create, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_CREATE, decode_create_maxsz, hdr);
	encode_uint32(xdr, create->ftype);

	switch (create->ftype) {
	case NF4LNK:
		p = reserve_space(xdr, 4);
		*p = cpu_to_be32(create->u.symlink.len);
		xdr_write_pages(xdr, create->u.symlink.pages, 0, create->u.symlink.len);
		break;

	case NF4BLK: case NF4CHR:
		p = reserve_space(xdr, 8);
		*p++ = cpu_to_be32(create->u.device.specdata1);
		*p = cpu_to_be32(create->u.device.specdata2);
		break;

	default:
		break;
	}

	encode_string(xdr, create->name->len, create->name->name);
	encode_attrs(xdr, create->attrs, create->label, create->server);
}

static void encode_getattr_one(struct xdr_stream *xdr, uint32_t bitmap, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_GETATTR, decode_getattr_maxsz, hdr);
	p = reserve_space(xdr, 8);
	*p++ = cpu_to_be32(1);
	*p = cpu_to_be32(bitmap);
}

static void encode_getattr_two(struct xdr_stream *xdr, uint32_t bm0, uint32_t bm1, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_GETATTR, decode_getattr_maxsz, hdr);
	p = reserve_space(xdr, 12);
	*p++ = cpu_to_be32(2);
	*p++ = cpu_to_be32(bm0);
	*p = cpu_to_be32(bm1);
}

static void
encode_getattr_three(struct xdr_stream *xdr,
		     uint32_t bm0, uint32_t bm1, uint32_t bm2,
		     struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_GETATTR, decode_getattr_maxsz, hdr);
	if (bm2) {
		p = reserve_space(xdr, 16);
		*p++ = cpu_to_be32(3);
		*p++ = cpu_to_be32(bm0);
		*p++ = cpu_to_be32(bm1);
		*p = cpu_to_be32(bm2);
	} else if (bm1) {
		p = reserve_space(xdr, 12);
		*p++ = cpu_to_be32(2);
		*p++ = cpu_to_be32(bm0);
		*p = cpu_to_be32(bm1);
	} else {
		p = reserve_space(xdr, 8);
		*p++ = cpu_to_be32(1);
		*p = cpu_to_be32(bm0);
	}
}

static void encode_getfattr(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr_three(xdr, bitmask[0] & nfs4_fattr_bitmap[0],
			   bitmask[1] & nfs4_fattr_bitmap[1],
			   bitmask[2] & nfs4_fattr_bitmap[2],
			   hdr);
}

static void encode_getfattr_open(struct xdr_stream *xdr, const u32 *bitmask,
				 const u32 *open_bitmap,
				 struct compound_hdr *hdr)
{
	encode_getattr_three(xdr,
			     bitmask[0] & open_bitmap[0],
			     bitmask[1] & open_bitmap[1],
			     bitmask[2] & open_bitmap[2],
			     hdr);
}

static void encode_fsinfo(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr_three(xdr,
			     bitmask[0] & nfs4_fsinfo_bitmap[0],
			     bitmask[1] & nfs4_fsinfo_bitmap[1],
			     bitmask[2] & nfs4_fsinfo_bitmap[2],
			     hdr);
}

static void encode_fs_locations(struct xdr_stream *xdr, const u32* bitmask, struct compound_hdr *hdr)
{
	encode_getattr_two(xdr, bitmask[0] & nfs4_fs_locations_bitmap[0],
			   bitmask[1] & nfs4_fs_locations_bitmap[1], hdr);
}

static void encode_getfh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_GETFH, decode_getfh_maxsz, hdr);
}

static void encode_link(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_LINK, decode_link_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

static inline int nfs4_lock_type(struct file_lock *fl, int block)
{
	if (fl->fl_type == F_RDLCK)
		return block ? NFS4_READW_LT : NFS4_READ_LT;
	return block ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
}

static inline uint64_t nfs4_lock_length(struct file_lock *fl)
{
	if (fl->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return fl->fl_end - fl->fl_start + 1;
}

static void encode_lockowner(struct xdr_stream *xdr, const struct nfs_lowner *lowner)
{
	__be32 *p;

	p = reserve_space(xdr, 32);
	p = xdr_encode_hyper(p, lowner->clientid);
	*p++ = cpu_to_be32(20);
	p = xdr_encode_opaque_fixed(p, "lock id:", 8);
	*p++ = cpu_to_be32(lowner->s_dev);
	xdr_encode_hyper(p, lowner->id);
}

/*
 * opcode,type,reclaim,offset,length,new_lock_owner = 32
 * open_seqid,open_stateid,lock_seqid,lock_owner.clientid, lock_owner.id = 40
 */
static void encode_lock(struct xdr_stream *xdr, const struct nfs_lock_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LOCK, decode_lock_maxsz, hdr);
	p = reserve_space(xdr, 28);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, args->block));
	*p++ = cpu_to_be32(args->reclaim);
	p = xdr_encode_hyper(p, args->fl->fl_start);
	p = xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	*p = cpu_to_be32(args->new_lock_owner);
	if (args->new_lock_owner){
		encode_nfs4_seqid(xdr, args->open_seqid);
		encode_nfs4_stateid(xdr, args->open_stateid);
		encode_nfs4_seqid(xdr, args->lock_seqid);
		encode_lockowner(xdr, &args->lock_owner);
	}
	else {
		encode_nfs4_stateid(xdr, args->lock_stateid);
		encode_nfs4_seqid(xdr, args->lock_seqid);
	}
}

static void encode_lockt(struct xdr_stream *xdr, const struct nfs_lockt_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LOCKT, decode_lockt_maxsz, hdr);
	p = reserve_space(xdr, 20);
	*p++ = cpu_to_be32(nfs4_lock_type(args->fl, 0));
	p = xdr_encode_hyper(p, args->fl->fl_start);
	p = xdr_encode_hyper(p, nfs4_lock_length(args->fl));
	encode_lockowner(xdr, &args->lock_owner);
}

static void encode_locku(struct xdr_stream *xdr, const struct nfs_locku_args *args, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LOCKU, decode_locku_maxsz, hdr);
	encode_uint32(xdr, nfs4_lock_type(args->fl, 0));
	encode_nfs4_seqid(xdr, args->seqid);
	encode_nfs4_stateid(xdr, args->stateid);
	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, args->fl->fl_start);
	xdr_encode_hyper(p, nfs4_lock_length(args->fl));
}

static void encode_release_lockowner(struct xdr_stream *xdr, const struct nfs_lowner *lowner, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RELEASE_LOCKOWNER, decode_release_lockowner_maxsz, hdr);
	encode_lockowner(xdr, lowner);
}

static void encode_lookup(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_LOOKUP, decode_lookup_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

static void encode_share_access(struct xdr_stream *xdr, fmode_t fmode)
{
	__be32 *p;

	p = reserve_space(xdr, 8);
	switch (fmode & (FMODE_READ|FMODE_WRITE)) {
	case FMODE_READ:
		*p++ = cpu_to_be32(NFS4_SHARE_ACCESS_READ);
		break;
	case FMODE_WRITE:
		*p++ = cpu_to_be32(NFS4_SHARE_ACCESS_WRITE);
		break;
	case FMODE_READ|FMODE_WRITE:
		*p++ = cpu_to_be32(NFS4_SHARE_ACCESS_BOTH);
		break;
	default:
		*p++ = cpu_to_be32(0);
	}
	*p = cpu_to_be32(0);		/* for linux, share_deny = 0 always */
}

static inline void encode_openhdr(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;
 /*
 * opcode 4, seqid 4, share_access 4, share_deny 4, clientid 8, ownerlen 4,
 * owner 4 = 32
 */
	encode_nfs4_seqid(xdr, arg->seqid);
	encode_share_access(xdr, arg->fmode);
	p = reserve_space(xdr, 36);
	p = xdr_encode_hyper(p, arg->clientid);
	*p++ = cpu_to_be32(24);
	p = xdr_encode_opaque_fixed(p, "open id:", 8);
	*p++ = cpu_to_be32(arg->server->s_dev);
	*p++ = cpu_to_be32(arg->id.uniquifier);
	xdr_encode_hyper(p, arg->id.create_time);
}

static inline void encode_createmode(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	struct iattr dummy;
	__be32 *p;

	p = reserve_space(xdr, 4);
	switch(arg->createmode) {
	case NFS4_CREATE_UNCHECKED:
		*p = cpu_to_be32(NFS4_CREATE_UNCHECKED);
		encode_attrs(xdr, arg->u.attrs, arg->label, arg->server);
		break;
	case NFS4_CREATE_GUARDED:
		*p = cpu_to_be32(NFS4_CREATE_GUARDED);
		encode_attrs(xdr, arg->u.attrs, arg->label, arg->server);
		break;
	case NFS4_CREATE_EXCLUSIVE:
		*p = cpu_to_be32(NFS4_CREATE_EXCLUSIVE);
		encode_nfs4_verifier(xdr, &arg->u.verifier);
		break;
	case NFS4_CREATE_EXCLUSIVE4_1:
		*p = cpu_to_be32(NFS4_CREATE_EXCLUSIVE4_1);
		encode_nfs4_verifier(xdr, &arg->u.verifier);
		dummy.ia_valid = 0;
		encode_attrs(xdr, &dummy, arg->label, arg->server);
	}
}

static void encode_opentype(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	switch (arg->open_flags & O_CREAT) {
	case 0:
		*p = cpu_to_be32(NFS4_OPEN_NOCREATE);
		break;
	default:
		*p = cpu_to_be32(NFS4_OPEN_CREATE);
		encode_createmode(xdr, arg);
	}
}

static inline void encode_delegation_type(struct xdr_stream *xdr, fmode_t delegation_type)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	switch (delegation_type) {
	case 0:
		*p = cpu_to_be32(NFS4_OPEN_DELEGATE_NONE);
		break;
	case FMODE_READ:
		*p = cpu_to_be32(NFS4_OPEN_DELEGATE_READ);
		break;
	case FMODE_WRITE|FMODE_READ:
		*p = cpu_to_be32(NFS4_OPEN_DELEGATE_WRITE);
		break;
	default:
		BUG();
	}
}

static inline void encode_claim_null(struct xdr_stream *xdr, const struct qstr *name)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_NULL);
	encode_string(xdr, name->len, name->name);
}

static inline void encode_claim_previous(struct xdr_stream *xdr, fmode_t type)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_PREVIOUS);
	encode_delegation_type(xdr, type);
}

static inline void encode_claim_delegate_cur(struct xdr_stream *xdr, const struct qstr *name, const nfs4_stateid *stateid)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_DELEGATE_CUR);
	encode_nfs4_stateid(xdr, stateid);
	encode_string(xdr, name->len, name->name);
}

static inline void encode_claim_fh(struct xdr_stream *xdr)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_FH);
}

static inline void encode_claim_delegate_cur_fh(struct xdr_stream *xdr, const nfs4_stateid *stateid)
{
	__be32 *p;

	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(NFS4_OPEN_CLAIM_DELEG_CUR_FH);
	encode_nfs4_stateid(xdr, stateid);
}

static void encode_open(struct xdr_stream *xdr, const struct nfs_openargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OPEN, decode_open_maxsz, hdr);
	encode_openhdr(xdr, arg);
	encode_opentype(xdr, arg);
	switch (arg->claim) {
	case NFS4_OPEN_CLAIM_NULL:
		encode_claim_null(xdr, arg->name);
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		encode_claim_previous(xdr, arg->u.delegation_type);
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		encode_claim_delegate_cur(xdr, arg->name, &arg->u.delegation);
		break;
	case NFS4_OPEN_CLAIM_FH:
		encode_claim_fh(xdr);
		break;
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
		encode_claim_delegate_cur_fh(xdr, &arg->u.delegation);
		break;
	default:
		BUG();
	}
}

static void encode_open_confirm(struct xdr_stream *xdr, const struct nfs_open_confirmargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OPEN_CONFIRM, decode_open_confirm_maxsz, hdr);
	encode_nfs4_stateid(xdr, arg->stateid);
	encode_nfs4_seqid(xdr, arg->seqid);
}

static void encode_open_downgrade(struct xdr_stream *xdr, const struct nfs_closeargs *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_OPEN_DOWNGRADE, decode_open_downgrade_maxsz, hdr);
	encode_nfs4_stateid(xdr, arg->stateid);
	encode_nfs4_seqid(xdr, arg->seqid);
	encode_share_access(xdr, arg->fmode);
}

static void
encode_putfh(struct xdr_stream *xdr, const struct nfs_fh *fh, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_PUTFH, decode_putfh_maxsz, hdr);
	encode_string(xdr, fh->size, fh->data);
}

static void encode_putrootfh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_PUTROOTFH, decode_putrootfh_maxsz, hdr);
}

static void encode_read(struct xdr_stream *xdr, const struct nfs_pgio_args *args,
			struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_READ, decode_read_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);

	p = reserve_space(xdr, 12);
	p = xdr_encode_hyper(p, args->offset);
	*p = cpu_to_be32(args->count);
}

static void encode_readdir(struct xdr_stream *xdr, const struct nfs4_readdir_arg *readdir, struct rpc_rqst *req, struct compound_hdr *hdr)
{
	uint32_t attrs[3] = {
		FATTR4_WORD0_RDATTR_ERROR,
		FATTR4_WORD1_MOUNTED_ON_FILEID,
	};
	uint32_t dircount = readdir->count >> 1;
	__be32 *p, verf[2];
	uint32_t attrlen = 0;
	unsigned int i;

	if (readdir->plus) {
		attrs[0] |= FATTR4_WORD0_TYPE|FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE|
			FATTR4_WORD0_FSID|FATTR4_WORD0_FILEHANDLE|FATTR4_WORD0_FILEID;
		attrs[1] |= FATTR4_WORD1_MODE|FATTR4_WORD1_NUMLINKS|FATTR4_WORD1_OWNER|
			FATTR4_WORD1_OWNER_GROUP|FATTR4_WORD1_RAWDEV|
			FATTR4_WORD1_SPACE_USED|FATTR4_WORD1_TIME_ACCESS|
			FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY;
		attrs[2] |= FATTR4_WORD2_SECURITY_LABEL;
		dircount >>= 1;
	}
	/* Use mounted_on_fileid only if the server supports it */
	if (!(readdir->bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID))
		attrs[0] |= FATTR4_WORD0_FILEID;
	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		attrs[i] &= readdir->bitmask[i];
		if (attrs[i] != 0)
			attrlen = i+1;
	}

	encode_op_hdr(xdr, OP_READDIR, decode_readdir_maxsz, hdr);
	encode_uint64(xdr, readdir->cookie);
	encode_nfs4_verifier(xdr, &readdir->verifier);
	p = reserve_space(xdr, 12 + (attrlen << 2));
	*p++ = cpu_to_be32(dircount);
	*p++ = cpu_to_be32(readdir->count);
	*p++ = cpu_to_be32(attrlen);
	for (i = 0; i < attrlen; i++)
		*p++ = cpu_to_be32(attrs[i]);
	memcpy(verf, readdir->verifier.data, sizeof(verf));

	dprintk("%s: cookie = %llu, verifier = %08x:%08x, bitmap = %08x:%08x:%08x\n",
			__func__,
			(unsigned long long)readdir->cookie,
			verf[0], verf[1],
			attrs[0] & readdir->bitmask[0],
			attrs[1] & readdir->bitmask[1],
			attrs[2] & readdir->bitmask[2]);
}

static void encode_readlink(struct xdr_stream *xdr, const struct nfs4_readlink *readlink, struct rpc_rqst *req, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_READLINK, decode_readlink_maxsz, hdr);
}

static void encode_remove(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_REMOVE, decode_remove_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

static void encode_rename(struct xdr_stream *xdr, const struct qstr *oldname, const struct qstr *newname, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RENAME, decode_rename_maxsz, hdr);
	encode_string(xdr, oldname->len, oldname->name);
	encode_string(xdr, newname->len, newname->name);
}

static void encode_renew(struct xdr_stream *xdr, clientid4 clid,
			 struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RENEW, decode_renew_maxsz, hdr);
	encode_uint64(xdr, clid);
}

static void
encode_restorefh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RESTOREFH, decode_restorefh_maxsz, hdr);
}

static void
encode_setacl(struct xdr_stream *xdr, struct nfs_setaclargs *arg, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_SETATTR, decode_setacl_maxsz, hdr);
	encode_nfs4_stateid(xdr, &zero_stateid);
	p = reserve_space(xdr, 2*4);
	*p++ = cpu_to_be32(1);
	*p = cpu_to_be32(FATTR4_WORD0_ACL);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(arg->acl_len);
	xdr_write_pages(xdr, arg->acl_pages, arg->acl_pgbase, arg->acl_len);
}

static void
encode_savefh(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SAVEFH, decode_savefh_maxsz, hdr);
}

static void encode_setattr(struct xdr_stream *xdr, const struct nfs_setattrargs *arg, const struct nfs_server *server, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SETATTR, decode_setattr_maxsz, hdr);
	encode_nfs4_stateid(xdr, &arg->stateid);
	encode_attrs(xdr, arg->iap, arg->label, server);
}

static void encode_setclientid(struct xdr_stream *xdr, const struct nfs4_setclientid *setclientid, struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_SETCLIENTID, decode_setclientid_maxsz, hdr);
	encode_nfs4_verifier(xdr, setclientid->sc_verifier);

	encode_string(xdr, setclientid->sc_name_len, setclientid->sc_name);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(setclientid->sc_prog);
	encode_string(xdr, setclientid->sc_netid_len, setclientid->sc_netid);
	encode_string(xdr, setclientid->sc_uaddr_len, setclientid->sc_uaddr);
	p = reserve_space(xdr, 4);
	*p = cpu_to_be32(setclientid->sc_cb_ident);
}

static void encode_setclientid_confirm(struct xdr_stream *xdr, const struct nfs4_setclientid_res *arg, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM,
			decode_setclientid_confirm_maxsz, hdr);
	encode_uint64(xdr, arg->clientid);
	encode_nfs4_verifier(xdr, &arg->confirm);
}

static void encode_write(struct xdr_stream *xdr, const struct nfs_pgio_args *args,
			 struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_WRITE, decode_write_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);

	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, args->offset);
	*p++ = cpu_to_be32(args->stable);
	*p = cpu_to_be32(args->count);

	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);
}

static void encode_delegreturn(struct xdr_stream *xdr, const nfs4_stateid *stateid, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DELEGRETURN, decode_delegreturn_maxsz, hdr);
	encode_nfs4_stateid(xdr, stateid);
}

static void encode_secinfo(struct xdr_stream *xdr, const struct qstr *name, struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SECINFO, decode_secinfo_maxsz, hdr);
	encode_string(xdr, name->len, name->name);
}

#if defined(CONFIG_NFS_V4_1)
/* NFSv4.1 operations */
static void encode_bind_conn_to_session(struct xdr_stream *xdr,
				   struct nfs4_session *session,
				   struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_BIND_CONN_TO_SESSION,
		decode_bind_conn_to_session_maxsz, hdr);
	encode_opaque_fixed(xdr, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
	p = xdr_reserve_space(xdr, 8);
	*p++ = cpu_to_be32(NFS4_CDFC4_BACK_OR_BOTH);
	*p = 0;	/* use_conn_in_rdma_mode = False */
}

static void encode_op_map(struct xdr_stream *xdr, struct nfs4_op_map *op_map)
{
	unsigned int i;
	encode_uint32(xdr, NFS4_OP_MAP_NUM_WORDS);
	for (i = 0; i < NFS4_OP_MAP_NUM_WORDS; i++)
		encode_uint32(xdr, op_map->u.words[i]);
}

static void encode_exchange_id(struct xdr_stream *xdr,
			       struct nfs41_exchange_id_args *args,
			       struct compound_hdr *hdr)
{
	__be32 *p;
	char impl_name[IMPL_NAME_LIMIT];
	int len = 0;

	encode_op_hdr(xdr, OP_EXCHANGE_ID, decode_exchange_id_maxsz, hdr);
	encode_nfs4_verifier(xdr, args->verifier);

	encode_string(xdr, args->id_len, args->id);

	encode_uint32(xdr, args->flags);
	encode_uint32(xdr, args->state_protect.how);

	switch (args->state_protect.how) {
	case SP4_NONE:
		break;
	case SP4_MACH_CRED:
		encode_op_map(xdr, &args->state_protect.enforce);
		encode_op_map(xdr, &args->state_protect.allow);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (send_implementation_id &&
	    sizeof(CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN) > 1 &&
	    sizeof(CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN)
		<= sizeof(impl_name) + 1)
		len = snprintf(impl_name, sizeof(impl_name), "%s %s %s %s",
			       utsname()->sysname, utsname()->release,
			       utsname()->version, utsname()->machine);

	if (len > 0) {
		encode_uint32(xdr, 1);	/* implementation id array length=1 */

		encode_string(xdr,
			sizeof(CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN) - 1,
			CONFIG_NFS_V4_1_IMPLEMENTATION_ID_DOMAIN);
		encode_string(xdr, len, impl_name);
		/* just send zeros for nii_date - the date is in nii_name */
		p = reserve_space(xdr, 12);
		p = xdr_encode_hyper(p, 0);
		*p = cpu_to_be32(0);
	} else
		encode_uint32(xdr, 0);	/* implementation id array length=0 */
}

static void encode_create_session(struct xdr_stream *xdr,
				  struct nfs41_create_session_args *args,
				  struct compound_hdr *hdr)
{
	__be32 *p;
	char machine_name[NFS4_MAX_MACHINE_NAME_LEN];
	uint32_t len;
	struct nfs_client *clp = args->client;
	struct nfs_net *nn = net_generic(clp->cl_net, nfs_net_id);
	u32 max_resp_sz_cached;

	/*
	 * Assumes OPEN is the biggest non-idempotent compound.
	 * 2 is the verifier.
	 */
	max_resp_sz_cached = (NFS4_dec_open_sz + RPC_REPHDRSIZE +
			      RPC_MAX_AUTH_SIZE + 2) * XDR_UNIT;

	len = scnprintf(machine_name, sizeof(machine_name), "%s",
			clp->cl_ipaddr);

	encode_op_hdr(xdr, OP_CREATE_SESSION, decode_create_session_maxsz, hdr);
	p = reserve_space(xdr, 16 + 2*28 + 20 + len + 12);
	p = xdr_encode_hyper(p, clp->cl_clientid);
	*p++ = cpu_to_be32(clp->cl_seqid);			/*Sequence id */
	*p++ = cpu_to_be32(args->flags);			/*flags */

	/* Fore Channel */
	*p++ = cpu_to_be32(0);				/* header padding size */
	*p++ = cpu_to_be32(args->fc_attrs.max_rqst_sz);	/* max req size */
	*p++ = cpu_to_be32(args->fc_attrs.max_resp_sz);	/* max resp size */
	*p++ = cpu_to_be32(max_resp_sz_cached);		/* Max resp sz cached */
	*p++ = cpu_to_be32(args->fc_attrs.max_ops);	/* max operations */
	*p++ = cpu_to_be32(args->fc_attrs.max_reqs);	/* max requests */
	*p++ = cpu_to_be32(0);				/* rdmachannel_attrs */

	/* Back Channel */
	*p++ = cpu_to_be32(0);				/* header padding size */
	*p++ = cpu_to_be32(args->bc_attrs.max_rqst_sz);	/* max req size */
	*p++ = cpu_to_be32(args->bc_attrs.max_resp_sz);	/* max resp size */
	*p++ = cpu_to_be32(args->bc_attrs.max_resp_sz_cached);	/* Max resp sz cached */
	*p++ = cpu_to_be32(args->bc_attrs.max_ops);	/* max operations */
	*p++ = cpu_to_be32(args->bc_attrs.max_reqs);	/* max requests */
	*p++ = cpu_to_be32(0);				/* rdmachannel_attrs */

	*p++ = cpu_to_be32(args->cb_program);		/* cb_program */
	*p++ = cpu_to_be32(1);
	*p++ = cpu_to_be32(RPC_AUTH_UNIX);			/* auth_sys */

	/* authsys_parms rfc1831 */
	*p++ = cpu_to_be32(nn->boot_time.tv_nsec);	/* stamp */
	p = xdr_encode_opaque(p, machine_name, len);
	*p++ = cpu_to_be32(0);				/* UID */
	*p++ = cpu_to_be32(0);				/* GID */
	*p = cpu_to_be32(0);				/* No more gids */
}

static void encode_destroy_session(struct xdr_stream *xdr,
				   struct nfs4_session *session,
				   struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DESTROY_SESSION, decode_destroy_session_maxsz, hdr);
	encode_opaque_fixed(xdr, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
}

static void encode_destroy_clientid(struct xdr_stream *xdr,
				   uint64_t clientid,
				   struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_DESTROY_CLIENTID, decode_destroy_clientid_maxsz, hdr);
	encode_uint64(xdr, clientid);
}

static void encode_reclaim_complete(struct xdr_stream *xdr,
				    struct nfs41_reclaim_complete_args *args,
				    struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_RECLAIM_COMPLETE, decode_reclaim_complete_maxsz, hdr);
	encode_uint32(xdr, args->one_fs);
}
#endif /* CONFIG_NFS_V4_1 */

static void encode_sequence(struct xdr_stream *xdr,
			    const struct nfs4_sequence_args *args,
			    struct compound_hdr *hdr)
{
#if defined(CONFIG_NFS_V4_1)
	struct nfs4_session *session;
	struct nfs4_slot_table *tp;
	struct nfs4_slot *slot = args->sa_slot;
	__be32 *p;

	tp = slot->table;
	session = tp->session;
	if (!session)
		return;

	encode_op_hdr(xdr, OP_SEQUENCE, decode_sequence_maxsz, hdr);

	/*
	 * Sessionid + seqid + slotid + max slotid + cache_this
	 */
	dprintk("%s: sessionid=%u:%u:%u:%u seqid=%d slotid=%d "
		"max_slotid=%d cache_this=%d\n",
		__func__,
		((u32 *)session->sess_id.data)[0],
		((u32 *)session->sess_id.data)[1],
		((u32 *)session->sess_id.data)[2],
		((u32 *)session->sess_id.data)[3],
		slot->seq_nr, slot->slot_nr,
		tp->highest_used_slotid, args->sa_cache_this);
	p = reserve_space(xdr, NFS4_MAX_SESSIONID_LEN + 16);
	p = xdr_encode_opaque_fixed(p, session->sess_id.data, NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(slot->seq_nr);
	*p++ = cpu_to_be32(slot->slot_nr);
	*p++ = cpu_to_be32(tp->highest_used_slotid);
	*p = cpu_to_be32(args->sa_cache_this);
#endif /* CONFIG_NFS_V4_1 */
}

#ifdef CONFIG_NFS_V4_1
static void
encode_getdeviceinfo(struct xdr_stream *xdr,
		     const struct nfs4_getdeviceinfo_args *args,
		     struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_GETDEVICEINFO, decode_getdeviceinfo_maxsz, hdr);
	p = reserve_space(xdr, NFS4_DEVICEID4_SIZE + 4 + 4);
	p = xdr_encode_opaque_fixed(p, args->pdev->dev_id.data,
				    NFS4_DEVICEID4_SIZE);
	*p++ = cpu_to_be32(args->pdev->layout_type);
	*p++ = cpu_to_be32(args->pdev->maxcount);	/* gdia_maxcount */

	p = reserve_space(xdr, 4 + 4);
	*p++ = cpu_to_be32(1);			/* bitmap length */
	*p++ = cpu_to_be32(NOTIFY_DEVICEID4_CHANGE | NOTIFY_DEVICEID4_DELETE);
}

static void
encode_layoutget(struct xdr_stream *xdr,
		      const struct nfs4_layoutget_args *args,
		      struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LAYOUTGET, decode_layoutget_maxsz, hdr);
	p = reserve_space(xdr, 36);
	*p++ = cpu_to_be32(0);     /* Signal layout available */
	*p++ = cpu_to_be32(args->type);
	*p++ = cpu_to_be32(args->range.iomode);
	p = xdr_encode_hyper(p, args->range.offset);
	p = xdr_encode_hyper(p, args->range.length);
	p = xdr_encode_hyper(p, args->minlength);
	encode_nfs4_stateid(xdr, &args->stateid);
	encode_uint32(xdr, args->maxcount);

	dprintk("%s: 1st type:0x%x iomode:%d off:%lu len:%lu mc:%d\n",
		__func__,
		args->type,
		args->range.iomode,
		(unsigned long)args->range.offset,
		(unsigned long)args->range.length,
		args->maxcount);
}

static int
encode_layoutcommit(struct xdr_stream *xdr,
		    struct inode *inode,
		    struct nfs4_layoutcommit_args *args,
		    struct compound_hdr *hdr)
{
	__be32 *p;

	dprintk("%s: lbw: %llu type: %d\n", __func__, args->lastbytewritten,
		NFS_SERVER(args->inode)->pnfs_curr_ld->id);

	encode_op_hdr(xdr, OP_LAYOUTCOMMIT, decode_layoutcommit_maxsz, hdr);
	p = reserve_space(xdr, 20);
	/* Only whole file layouts */
	p = xdr_encode_hyper(p, 0); /* offset */
	p = xdr_encode_hyper(p, args->lastbytewritten + 1);	/* length */
	*p = cpu_to_be32(0); /* reclaim */
	encode_nfs4_stateid(xdr, &args->stateid);
	p = reserve_space(xdr, 20);
	*p++ = cpu_to_be32(1); /* newoffset = TRUE */
	p = xdr_encode_hyper(p, args->lastbytewritten);
	*p++ = cpu_to_be32(0); /* Never send time_modify_changed */
	*p++ = cpu_to_be32(NFS_SERVER(args->inode)->pnfs_curr_ld->id);/* type */

	if (NFS_SERVER(inode)->pnfs_curr_ld->encode_layoutcommit) {
		NFS_SERVER(inode)->pnfs_curr_ld->encode_layoutcommit(
			NFS_I(inode)->layout, xdr, args);
	} else {
		encode_uint32(xdr, args->layoutupdate_len);
		if (args->layoutupdate_pages) {
			xdr_write_pages(xdr, args->layoutupdate_pages, 0,
					args->layoutupdate_len);
		}
	}

	return 0;
}

static void
encode_layoutreturn(struct xdr_stream *xdr,
		    const struct nfs4_layoutreturn_args *args,
		    struct compound_hdr *hdr)
{
	__be32 *p;

	encode_op_hdr(xdr, OP_LAYOUTRETURN, decode_layoutreturn_maxsz, hdr);
	p = reserve_space(xdr, 16);
	*p++ = cpu_to_be32(0);		/* reclaim. always 0 for now */
	*p++ = cpu_to_be32(args->layout_type);
	*p++ = cpu_to_be32(IOMODE_ANY);
	*p = cpu_to_be32(RETURN_FILE);
	p = reserve_space(xdr, 16);
	p = xdr_encode_hyper(p, 0);
	p = xdr_encode_hyper(p, NFS4_MAX_UINT64);
	spin_lock(&args->inode->i_lock);
	encode_nfs4_stateid(xdr, &args->stateid);
	spin_unlock(&args->inode->i_lock);
	if (NFS_SERVER(args->inode)->pnfs_curr_ld->encode_layoutreturn) {
		NFS_SERVER(args->inode)->pnfs_curr_ld->encode_layoutreturn(
			NFS_I(args->inode)->layout, xdr, args);
	} else
		encode_uint32(xdr, 0);
}

static int
encode_secinfo_no_name(struct xdr_stream *xdr,
		       const struct nfs41_secinfo_no_name_args *args,
		       struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_SECINFO_NO_NAME, decode_secinfo_no_name_maxsz, hdr);
	encode_uint32(xdr, args->style);
	return 0;
}

static void encode_test_stateid(struct xdr_stream *xdr,
				struct nfs41_test_stateid_args *args,
				struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_TEST_STATEID, decode_test_stateid_maxsz, hdr);
	encode_uint32(xdr, 1);
	encode_nfs4_stateid(xdr, args->stateid);
}

static void encode_free_stateid(struct xdr_stream *xdr,
				struct nfs41_free_stateid_args *args,
				struct compound_hdr *hdr)
{
	encode_op_hdr(xdr, OP_FREE_STATEID, decode_free_stateid_maxsz, hdr);
	encode_nfs4_stateid(xdr, &args->stateid);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

static u32 nfs4_xdr_minorversion(const struct nfs4_sequence_args *args)
{
#if defined(CONFIG_NFS_V4_1)
	struct nfs4_session *session = args->sa_slot->table->session;
	if (session)
		return session->clp->cl_mvops->minor_version;
#endif /* CONFIG_NFS_V4_1 */
	return 0;
}

/*
 * Encode an ACCESS request
 */
static void nfs4_xdr_enc_access(struct rpc_rqst *req, struct xdr_stream *xdr,
				const struct nfs4_accessargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_access(xdr, args->access, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LOOKUP request
 */
static void nfs4_xdr_enc_lookup(struct rpc_rqst *req, struct xdr_stream *xdr,
				const struct nfs4_lookup_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_lookup(xdr, args->name, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LOOKUP_ROOT request
 */
static void nfs4_xdr_enc_lookup_root(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const struct nfs4_lookup_root_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putrootfh(xdr, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode REMOVE request
 */
static void nfs4_xdr_enc_remove(struct rpc_rqst *req, struct xdr_stream *xdr,
				const struct nfs_removeargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_remove(xdr, &args->name, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode RENAME request
 */
static void nfs4_xdr_enc_rename(struct rpc_rqst *req, struct xdr_stream *xdr,
				const struct nfs_renameargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->old_dir, &hdr);
	encode_savefh(xdr, &hdr);
	encode_putfh(xdr, args->new_dir, &hdr);
	encode_rename(xdr, args->old_name, args->new_name, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LINK request
 */
static void nfs4_xdr_enc_link(struct rpc_rqst *req, struct xdr_stream *xdr,
			     const struct nfs4_link_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_savefh(xdr, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_link(xdr, args->name, &hdr);
	encode_restorefh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode CREATE request
 */
static void nfs4_xdr_enc_create(struct rpc_rqst *req, struct xdr_stream *xdr,
				const struct nfs4_create_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_create(xdr, args, &hdr);
	encode_getfh(xdr, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode SYMLINK request
 */
static void nfs4_xdr_enc_symlink(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const struct nfs4_create_arg *args)
{
	nfs4_xdr_enc_create(req, xdr, args);
}

/*
 * Encode GETATTR request
 */
static void nfs4_xdr_enc_getattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const struct nfs4_getattr_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a CLOSE request
 */
static void nfs4_xdr_enc_close(struct rpc_rqst *req, struct xdr_stream *xdr,
			       struct nfs_closeargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_close(xdr, args, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode an OPEN request
 */
static void nfs4_xdr_enc_open(struct rpc_rqst *req, struct xdr_stream *xdr,
			      struct nfs_openargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open(xdr, args, &hdr);
	encode_getfh(xdr, &hdr);
	if (args->access)
		encode_access(xdr, args->access, &hdr);
	encode_getfattr_open(xdr, args->bitmask, args->open_bitmap, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode an OPEN_CONFIRM request
 */
static void nfs4_xdr_enc_open_confirm(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      struct nfs_open_confirmargs *args)
{
	struct compound_hdr hdr = {
		.nops   = 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open_confirm(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode an OPEN request with no attributes.
 */
static void nfs4_xdr_enc_open_noattr(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     struct nfs_openargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open(xdr, args, &hdr);
	if (args->access)
		encode_access(xdr, args->access, &hdr);
	encode_getfattr_open(xdr, args->bitmask, args->open_bitmap, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode an OPEN_DOWNGRADE request
 */
static void nfs4_xdr_enc_open_downgrade(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					struct nfs_closeargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_open_downgrade(xdr, args, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a LOCK request
 */
static void nfs4_xdr_enc_lock(struct rpc_rqst *req, struct xdr_stream *xdr,
			      struct nfs_lock_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_lock(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a LOCKT request
 */
static void nfs4_xdr_enc_lockt(struct rpc_rqst *req, struct xdr_stream *xdr,
			       struct nfs_lockt_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_lockt(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a LOCKU request
 */
static void nfs4_xdr_enc_locku(struct rpc_rqst *req, struct xdr_stream *xdr,
			       struct nfs_locku_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_locku(xdr, args, &hdr);
	encode_nops(&hdr);
}

static void nfs4_xdr_enc_release_lockowner(struct rpc_rqst *req,
					   struct xdr_stream *xdr,
					struct nfs_release_lockowner_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_release_lockowner(xdr, &args->lock_owner, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a READLINK request
 */
static void nfs4_xdr_enc_readlink(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const struct nfs4_readlink *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_readlink(xdr, args, req, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2, args->pages,
			args->pgbase, args->pglen);
	encode_nops(&hdr);
}

/*
 * Encode a READDIR request
 */
static void nfs4_xdr_enc_readdir(struct rpc_rqst *req, struct xdr_stream *xdr,
				 const struct nfs4_readdir_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_readdir(xdr, args, req, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2, args->pages,
			 args->pgbase, args->count);
	dprintk("%s: inlined page args = (%u, %p, %u, %u)\n",
			__func__, hdr.replen << 2, args->pages,
			args->pgbase, args->count);
	encode_nops(&hdr);
}

/*
 * Encode a READ request
 */
static void nfs4_xdr_enc_read(struct rpc_rqst *req, struct xdr_stream *xdr,
			      struct nfs_pgio_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_read(xdr, args, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2,
			 args->pages, args->pgbase, args->count);
	req->rq_rcv_buf.flags |= XDRBUF_READ;
	encode_nops(&hdr);
}

/*
 * Encode an SETATTR request
 */
static void nfs4_xdr_enc_setattr(struct rpc_rqst *req, struct xdr_stream *xdr,
				 struct nfs_setattrargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_setattr(xdr, args, args->server, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode a GETACL request
 */
static void nfs4_xdr_enc_getacl(struct rpc_rqst *req, struct xdr_stream *xdr,
				struct nfs_getaclargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	replen = hdr.replen + op_decode_hdr_maxsz + 1;
	encode_getattr_two(xdr, FATTR4_WORD0_ACL, 0, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, replen << 2,
		args->acl_pages, args->acl_pgbase, args->acl_len);

	encode_nops(&hdr);
}

/*
 * Encode a WRITE request
 */
static void nfs4_xdr_enc_write(struct rpc_rqst *req, struct xdr_stream *xdr,
			       struct nfs_pgio_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_write(xdr, args, &hdr);
	req->rq_snd_buf.flags |= XDRBUF_WRITE;
	if (args->bitmask)
		encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 *  a COMMIT request
 */
static void nfs4_xdr_enc_commit(struct rpc_rqst *req, struct xdr_stream *xdr,
				struct nfs_commitargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_commit(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * FSINFO request
 */
static void nfs4_xdr_enc_fsinfo(struct rpc_rqst *req, struct xdr_stream *xdr,
				struct nfs4_fsinfo_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_fsinfo(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * a PATHCONF request
 */
static void nfs4_xdr_enc_pathconf(struct rpc_rqst *req, struct xdr_stream *xdr,
				  const struct nfs4_pathconf_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getattr_one(xdr, args->bitmask[0] & nfs4_pathconf_bitmap[0],
			   &hdr);
	encode_nops(&hdr);
}

/*
 * a STATFS request
 */
static void nfs4_xdr_enc_statfs(struct rpc_rqst *req, struct xdr_stream *xdr,
				const struct nfs4_statfs_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getattr_two(xdr, args->bitmask[0] & nfs4_statfs_bitmap[0],
			   args->bitmask[1] & nfs4_statfs_bitmap[1], &hdr);
	encode_nops(&hdr);
}

/*
 * GETATTR_BITMAP request
 */
static void nfs4_xdr_enc_server_caps(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     struct nfs4_server_caps_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fhandle, &hdr);
	encode_getattr_one(xdr, FATTR4_WORD0_SUPPORTED_ATTRS|
			   FATTR4_WORD0_FH_EXPIRE_TYPE|
			   FATTR4_WORD0_LINK_SUPPORT|
			   FATTR4_WORD0_SYMLINK_SUPPORT|
			   FATTR4_WORD0_ACLSUPPORT, &hdr);
	encode_nops(&hdr);
}

/*
 * a RENEW request
 */
static void nfs4_xdr_enc_renew(struct rpc_rqst *req, struct xdr_stream *xdr,
			       struct nfs_client *clp)
{
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_renew(xdr, clp->cl_clientid, &hdr);
	encode_nops(&hdr);
}

/*
 * a SETCLIENTID request
 */
static void nfs4_xdr_enc_setclientid(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     struct nfs4_setclientid *sc)
{
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_setclientid(xdr, sc, &hdr);
	encode_nops(&hdr);
}

/*
 * a SETCLIENTID_CONFIRM request
 */
static void nfs4_xdr_enc_setclientid_confirm(struct rpc_rqst *req,
					     struct xdr_stream *xdr,
					     struct nfs4_setclientid_res *arg)
{
	struct compound_hdr hdr = {
		.nops	= 0,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_setclientid_confirm(xdr, arg, &hdr);
	encode_nops(&hdr);
}

/*
 * DELEGRETURN request
 */
static void nfs4_xdr_enc_delegreturn(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     const struct nfs4_delegreturnargs *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fhandle, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_delegreturn(xdr, args->stateid, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode FS_LOCATIONS request
 */
static void nfs4_xdr_enc_fs_locations(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      struct nfs4_fs_locations_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};
	uint32_t replen;

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	if (args->migration) {
		encode_putfh(xdr, args->fh, &hdr);
		replen = hdr.replen;
		encode_fs_locations(xdr, args->bitmask, &hdr);
		if (args->renew)
			encode_renew(xdr, args->clientid, &hdr);
	} else {
		encode_putfh(xdr, args->dir_fh, &hdr);
		encode_lookup(xdr, args->name, &hdr);
		replen = hdr.replen;
		encode_fs_locations(xdr, args->bitmask, &hdr);
	}

	/* Set up reply kvec to capture returned fs_locations array. */
	xdr_inline_pages(&req->rq_rcv_buf, replen << 2, &args->page,
			0, PAGE_SIZE);
	encode_nops(&hdr);
}

/*
 * Encode SECINFO request
 */
static void nfs4_xdr_enc_secinfo(struct rpc_rqst *req,
				struct xdr_stream *xdr,
				struct nfs4_secinfo_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->dir_fh, &hdr);
	encode_secinfo(xdr, args->name, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode FSID_PRESENT request
 */
static void nfs4_xdr_enc_fsid_present(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      struct nfs4_fsid_present_arg *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, args->fh, &hdr);
	encode_getfh(xdr, &hdr);
	if (args->renew)
		encode_renew(xdr, args->clientid, &hdr);
	encode_nops(&hdr);
}

#if defined(CONFIG_NFS_V4_1)
/*
 * BIND_CONN_TO_SESSION request
 */
static void nfs4_xdr_enc_bind_conn_to_session(struct rpc_rqst *req,
				struct xdr_stream *xdr,
				struct nfs_client *clp)
{
	struct compound_hdr hdr = {
		.minorversion = clp->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_bind_conn_to_session(xdr, clp->cl_session, &hdr);
	encode_nops(&hdr);
}

/*
 * EXCHANGE_ID request
 */
static void nfs4_xdr_enc_exchange_id(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     struct nfs41_exchange_id_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_exchange_id(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * a CREATE_SESSION request
 */
static void nfs4_xdr_enc_create_session(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					struct nfs41_create_session_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = args->client->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_create_session(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * a DESTROY_SESSION request
 */
static void nfs4_xdr_enc_destroy_session(struct rpc_rqst *req,
					 struct xdr_stream *xdr,
					 struct nfs4_session *session)
{
	struct compound_hdr hdr = {
		.minorversion = session->clp->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_destroy_session(xdr, session, &hdr);
	encode_nops(&hdr);
}

/*
 * a DESTROY_CLIENTID request
 */
static void nfs4_xdr_enc_destroy_clientid(struct rpc_rqst *req,
					 struct xdr_stream *xdr,
					 struct nfs_client *clp)
{
	struct compound_hdr hdr = {
		.minorversion = clp->cl_mvops->minor_version,
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_destroy_clientid(xdr, clp->cl_clientid, &hdr);
	encode_nops(&hdr);
}

/*
 * a SEQUENCE request
 */
static void nfs4_xdr_enc_sequence(struct rpc_rqst *req, struct xdr_stream *xdr,
				  struct nfs4_sequence_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * a GET_LEASE_TIME request
 */
static void nfs4_xdr_enc_get_lease_time(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					struct nfs4_get_lease_time_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->la_seq_args),
	};
	const u32 lease_bitmap[3] = { FATTR4_WORD0_LEASE_TIME };

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->la_seq_args, &hdr);
	encode_putrootfh(xdr, &hdr);
	encode_fsinfo(xdr, lease_bitmap, &hdr);
	encode_nops(&hdr);
}

/*
 * a RECLAIM_COMPLETE request
 */
static void nfs4_xdr_enc_reclaim_complete(struct rpc_rqst *req,
					  struct xdr_stream *xdr,
				struct nfs41_reclaim_complete_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args)
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_reclaim_complete(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode GETDEVICEINFO request
 */
static void nfs4_xdr_enc_getdeviceinfo(struct rpc_rqst *req,
				       struct xdr_stream *xdr,
				       struct nfs4_getdeviceinfo_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_getdeviceinfo(xdr, args, &hdr);

	/* set up reply kvec. Subtract notification bitmap max size (2)
	 * so that notification bitmap is put in xdr_buf tail */
	xdr_inline_pages(&req->rq_rcv_buf, (hdr.replen - 2) << 2,
			 args->pdev->pages, args->pdev->pgbase,
			 args->pdev->pglen);

	encode_nops(&hdr);
}

/*
 *  Encode LAYOUTGET request
 */
static void nfs4_xdr_enc_layoutget(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   struct nfs4_layoutget_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	encode_layoutget(xdr, args, &hdr);

	xdr_inline_pages(&req->rq_rcv_buf, hdr.replen << 2,
	    args->layout.pages, 0, args->layout.pglen);

	encode_nops(&hdr);
}

/*
 *  Encode LAYOUTCOMMIT request
 */
static void nfs4_xdr_enc_layoutcommit(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      struct nfs4_layoutcommit_args *args)
{
	struct nfs4_layoutcommit_data *data =
		container_of(args, struct nfs4_layoutcommit_data, args);
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	encode_layoutcommit(xdr, data->args.inode, args, &hdr);
	encode_getfattr(xdr, args->bitmask, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode LAYOUTRETURN request
 */
static void nfs4_xdr_enc_layoutreturn(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      struct nfs4_layoutreturn_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putfh(xdr, NFS_FH(args->inode), &hdr);
	encode_layoutreturn(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 * Encode SECINFO_NO_NAME request
 */
static int nfs4_xdr_enc_secinfo_no_name(struct rpc_rqst *req,
					struct xdr_stream *xdr,
					struct nfs41_secinfo_no_name_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_putrootfh(xdr, &hdr);
	encode_secinfo_no_name(xdr, args, &hdr);
	encode_nops(&hdr);
	return 0;
}

/*
 *  Encode TEST_STATEID request
 */
static void nfs4_xdr_enc_test_stateid(struct rpc_rqst *req,
				      struct xdr_stream *xdr,
				      struct nfs41_test_stateid_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_test_stateid(xdr, args, &hdr);
	encode_nops(&hdr);
}

/*
 *  Encode FREE_STATEID request
 */
static void nfs4_xdr_enc_free_stateid(struct rpc_rqst *req,
				     struct xdr_stream *xdr,
				     struct nfs41_free_stateid_args *args)
{
	struct compound_hdr hdr = {
		.minorversion = nfs4_xdr_minorversion(&args->seq_args),
	};

	encode_compound_hdr(xdr, req, &hdr);
	encode_sequence(xdr, &args->seq_args, &hdr);
	encode_free_stateid(xdr, args, &hdr);
	encode_nops(&hdr);
}
#endif /* CONFIG_NFS_V4_1 */

static void print_overflow_msg(const char *func, const struct xdr_stream *xdr)
{
	dprintk("nfs: %s: prematurely hit end of receive buffer. "
		"Remaining buffer length is %tu words.\n",
		func, xdr->end - xdr->p);
}

static int decode_opaque_inline(struct xdr_stream *xdr, unsigned int *len, char **string)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, *len);
	if (unlikely(!p))
		goto out_overflow;
	*string = (char *)p;
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	hdr->status = be32_to_cpup(p++);
	hdr->taglen = be32_to_cpup(p);

	p = xdr_inline_decode(xdr, hdr->taglen + 4);
	if (unlikely(!p))
		goto out_overflow;
	hdr->tag = (char *)p;
	p += XDR_QUADLEN(hdr->taglen);
	hdr->nops = be32_to_cpup(p);
	if (unlikely(hdr->nops < 1))
		return nfs4_stat_to_errno(hdr->status);
	return 0;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected,
		int *nfs_retval)
{
	__be32 *p;
	uint32_t opnum;
	int32_t nfserr;

	p = xdr_inline_decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cpup(p++);
	if (unlikely(opnum != expected))
		goto out_bad_operation;
	nfserr = be32_to_cpup(p);
	if (nfserr == NFS_OK)
		*nfs_retval = 0;
	else
		*nfs_retval = nfs4_stat_to_errno(nfserr);
	return true;
out_bad_operation:
	dprintk("nfs: Server returned operation"
		" %d but we issued a request for %d\n",
			opnum, expected);
	*nfs_retval = -EREMOTEIO;
	return false;
out_overflow:
	print_overflow_msg(__func__, xdr);
	*nfs_retval = -EIO;
	return false;
}

static int decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	int retval;

	__decode_op_hdr(xdr, expected, &operati_
	eneration:
	dprintk("nf= -
out_ovet_overf;y rint);
s4_xdr_enc_seci, expecaeam *xdr,
			    const strlow_ms*aea,t *clp)
{
	struct compound_h2_t opnum;
	i char **stri *clocatiDR_QUA *ce_decode(xdr, 8);
	if (unlikely12 NFS_OK)xpectedpstat_to_errnine(struct xdr_streamrgs, & *cloc, & *ce_dec(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struTR4_Wxdr_bum *xdr, unsigned int *len,	p = xdr*xdr_buund_h,	p = xdrbmlocatixdr_inline_decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpbmloc);

	p = xdr_inline_deargs->bit);

	encode_);

	encod2]s_retvacode(xdr, 4);
	if (unlikely(bmloc)layo)!p))
		goto out_overflow;
	*len = be32_to_cp
		gbmloc)> 0h(xdr,args->bit);


	if (unlikely(opnup
		gbmloc)> 1pages(x
	encode_);


	if (unlikely(opnupp
		gbmloc)> 2(xdr,	
	encod2]s_r	if (nfserr == NFS
}

sterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struTR4_Wds.\n"m *xdr, unsigned int *len,	p = xdr*TR4_loc, , char **strin;
	eound_h2_t opnum;
decode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpuTR4_locs_r	if (nfserr == NFSn;
	eoode(xdrgned i_poask, _overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wsuppornerm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p = xdr*xdr_battr{FS_OK)xpectedargs->bit)&PORTED_ATTRS|
			   FATTR4_WO)pages(ecode_o;at_to_odedr(struTR4_Wxdr_bum *lenxdr_battpnup
		goto out_oto_o< 0h(xdr,tk("nf= -
;dr,args->bit)&= ~ORTED_ATTRS|
			   FATTR4_WO;_uint32(xdr_statfs_bit= _statfs_bit= _statfs_2]s_retvaned page args_statfs=%08x:%08x:%08xgs->lastbytewrxdr_statfs_bi, _statfs_bi, _statfs_2]/*
 *  Encode TESode_op_nd_hdr(struTR4_Wu_tom *xdr, unsigned int *len,	p = xdr*xdr_buen,	p = xdr*u_to_nd_h2_t opnum;
	ecode_os_retvFSn_SERV_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
4_WOati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
4_WO)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSn_SERV_r	if (nfserr == NFS

		gn_SERV<etv4REG ||r*u_to)> tv4statDRTEDpages(xned page args_adS_SERVargs->lastbytewri*u_to_be3t_to_errnse;
}


ste,args->bit)&= ~ORTED_ATTRS|
4_WO;at_to_odetvalst
 *ORTED
4_WO;at}vaned page argsu_to=0%ogs->lastbytewri{
	su_to2fmt[*u_to]printk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wfh_i_
ireWu_tom *xdr, unsigned int *le nfs41_test,	p = xdr*xdr_buen,	p = xdr*u_to_nd_h2_t opnum;
FSn_SERV_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
		   FATTR4_WOati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
		   FATTR4_WO)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSn_SERV_r	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
		   FATTR4_WO;at}vaned page argsi_
iresu_to=0x%xgs->lastbytewri*u_to_be3rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wgs, &hm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*gs, &h_nd_h2_t opnum;
	ecode_os_retvFSngs, &hV_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|

 */
sati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|

 */
s)pages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);gs, &h_NFS
args->bit)&= ~ORTED_ATTRS|

 */
s;at_to_odetvalst
 *ORTED

 */
s;at}vaned page argsgs, &hVtatic voi=%Lugs->lastbytewrxdrrgs->range.leng.lengt*gs, &h_NFStk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_What m *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*hat _nd_h2_t opnum;
	ecode_os_retvFSnhat n_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
ps(&ati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
ps(&hpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);hat _;dr,args->bit)&= ~ORTED_ATTRS|
	s(&;at_to_odetvalst
 *ORTED
	s(&;at}vaned page argsfile;hat =%Lugs->lastbytewr gs->range.leng.lengt*hat _;drtk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_W
	strsuppornm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p = xdr*res_nd_h2_t opnum;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
   FATTR4_WOati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
   FATTR4_WO)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnct c_r	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
   FATTR4_WO;at}vaned page argsruct supporn=%sgs->lastbytewri*ct c__re ? "int d" : "rati"_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Ws_rqst rsuppornm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p = xdr*res_nd_h2_t opnum;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
			   FATTR4_WOati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
p		   FATTR4_WO)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnct c_r	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
p		   FATTR4_WO;at}vaned page argss_rqst  supporn=%sgs->lastbytewri*ct c__re ? "int d" : "rati"_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_W*argm *xdr, unsigned int *len,	p = xdr*xdr_buen *clp)
{
	s*arg **arg_nd_h2_t opnum;
	ecode_os_retvFS*arg->majorc_retva*arg->m->seV_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
	SIDati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
	SID)pages(code(xdr, 4);
	if (unlikely16!p)))
		goto out_overfloww;
	*len = be32_to_cpScode(xdr+ 1;
	enUINT64);&*arg->major)_cpS(xdr+ 1;
	enUINT64);&*arg->m->se NFS
args->bit)&= ~ORTED_ATTRS|
	SID;at_to_odetvalst
 *ORTED
	SID;at}vaned page argsfarg=(0x%Lx/0x%Lx)gs->lastbytewrxdrrgs->range.leng.lengt*arg->majorrxdrrgs->range.leng.lengt*arg->m->se NFStk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_W
rpc_rqst *req,
		unsigned int *len,	p = xdr*xdr_buen,	p = xdr*res_nd_h2_t opnum;
FSnct c_r6etva
		goto out_oargs->bit)&P(ORTED_ATTRS|
 code_compoti1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
 code_comp)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnct c_r	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
 code_comp;at}vaned page argsfile;hat =%ugs->lastbytewr gs->range. pa)*res_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Werroam *xdr, enum nfs_opnum4 e,	p = xdr*xdr_buen	p = xdr*res_nd_h2_t opnum;
FS
		goto out_oargs->bit)&P(ORTED_ATTRS|
RDRTED_ERRORoti1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
RDRTED_ERROR)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSargs->bit)&= ~ORTED_ATTRS|
RDRTED_ERROR_cpSnct c_r-	if (nfserr == NFSsterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struTR4_Wfilencode_m *xdr, unsigned int *len,	p = xdr*xdr_buen *clp)
{
	s*h **h_nd_h2_t opnum;
	ecodpound_h
		g*h !detULLflowmemset(e_ge0);hat of(**h_);
FS
		goto out_oargs->bit)&P(ORTED_ATTRS|
FILE */DLOati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
	ILE */DLO)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlocs_r	if (nfserr == NFS
_OK)xoc)> tva4_FHps(&he3t_to_errnse;
}


code(xdr, 4);
	if (unlikely= be32_)
		goto out_overfloww;
	*len = be32_to_cpS
		g*h !detULLfages(xmemcpyg*h->uct coply= be32_)	*h->hat n_rlocatioste,args->bit)&= ~ORTED_ATTRS|
	ILE */DLONFSsterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struTR4_Waclsuppornm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p = xdr*res_nd_h2_t opnum;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
;
	encode_ati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
;
	encode_)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnct c_r	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
;
	encode_;at}vaned page args;
	s supporner=%ugs->lastbytewr gs->range. pa)*res_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wfilergm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*filerg_nd_h2_t opnum;
	ecode_os_retvFSnfilergV_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
	ILEIDati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
	ILEIDhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);filerg_NFS
args->bit)&= ~ORTED_ATTRS|
	ILEID;at_to_odetvalst
 *ORTED
	ILEID;at}vaned page argsfilerg=%Lugs->lastbytewr gs->range.leng.lengt*filerg_NFStk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wmcv_bed_onWfilergm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*filerg_nd_h2_t opnum;
	ecode_os_retvFSnfilergV_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_MOUNFATTON
	ILEIDati1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_MOUNFATTON
	ILEIDhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);filerg_NFS
args->b1t)&= ~ORTED_ATTRS1_MOUNFATTON
	ILEID;at_to_odetvalst
 *ORTED
MOUNFATTON
	ILEID;at}vaned page argsfilerg=%Lugs->lastbytewr gs->range.leng.lengt*filerg_NFStk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wfiles_availm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
	ILEalsVAILati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
	ILEalsVAILhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->bit)&= ~ORTED_ATTRS|
	ILEalsVAIL;at}vaned page argsfiles avail=%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wfiles_ argm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
	ILEalest
ati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
	ILEalest
hpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->bit)&= ~ORTED_ATTRS|
	ILEalest
;at}vaned page argsfiles  arg=%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wfiles_totalm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
	ILEalTOTALati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
	ILEalTOTALhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->bit)&= ~ORTED_ATTRS|
	ILEalTOTAL;at}vaned page argsfiles total=%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(stru],
	qst *req,
		unsigned int *lentreturn_args],
	qst num,
	und_h,opncatixdr_inline_	stri *up(p++)0;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpcs_r	if (nfserr == NFS_OK)
c__reflow;
	*l;
	es],
	;vaned page ],
	qst 4: " NFS_OK)
c> tva4_ */
stat_MAX */
ONENTSpages(ned page cannot parsRVarr)
{
	nents*/
	],
	gs->lbe32_);
	*len =eio;at}vaum, (],
	->n)
{
	nents*+)0; ],
	->n)
{
	nents*<nca ],
	->n)
{
	nents++pages(nce_args *arg;
	ret*)
{
	nent*+)&],
	->)
{
	nents[],
	->n)
{
	nents];es(ncup(p++)ine(struct xdr_streamrgs, &)
{
	nent->loc, &)
{
	nent->uct !p)))
		goto out_oncup(p+!= 0h(xdr,;
	*len =eio;at)
	debug (XDR(xdr,prxdr,te ar%.*s cted);	(],
	->n)
{
	nents*!= n ? "/ " : "")t nfs4_s{
	nent->loc, )
{
	nent->uct !p))}rint:verflow:
 *up(ppr;
	es],
	:t_oval;
	e ],
	qst nis sent*as a zero )
{
	nent4ages(],
	->n)
{
	nents*+)r_tw],
	->)
{
	nents[0].loc=0_tw],
	->)
{
	nents[0].uct =tULL;vaned page ],
	qst 4: /gs-!p));
	*len print_eioServer returi *up(p+%d", 0;
out_ovencup(p++)se;
}

;
	*len print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_W*a rpc_rqst *req,
		unsigned int *len,	p = xdr*xdr_buen *clp)
{
	g *args)
{
	str*res_nd_hnd_hcatixdr_inline_	stri *up(p++)ool __va
		goto out_oargs->bit)&P(ORTED_ATTRS|
	St
 */
stati-1U)stat_;
	*len pr	 *up(p++)0;
)
		goto out_ovdargs->bit)&PORTED_ATTRS|
	St
 */
stat)stat_;
	*len pr	 *up(p++)se;
}

_ovIgnore borke:
 eratisn bitmtatic bun,
			op%d\nR4_sages(
		goto out_oto c__rtULLftat_;
	*len pr	ned page argsfa;
	e:gs->lastbytew_ovencup(p++)dr(stru],
	qst *rgs, &to ->s)
{,
	u;
)
		goto out_oncup(p+!= 0h(xdr;
	*len pr	code(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpcs_r	if (nfserr == NFS_OK)
c<_reflow;
	*len =eio;atum, (to ->ngs)
{
	str+)0; to ->ngs)
{
	str<nca to ->ngs)
{
	st++pages(,opnm;
nfo_arg *args)
{rgs)
{
	s *gs)__vaS_OK)to ->ngs)
{
	str+= tva4_FSt
 */
stat_MAXENTRIES(xdr,bed k_cpSloc*+)&to ->gs)
{
	st[to ->ngs)
{
	st]}


code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSp++);
	if (unlikel);
FSaned page argsseratis:gs->lastbytew_ovetum, (gs)->n eratisn+)0; gs)->n eratisn<nm; gs)->n eratis++pages((nce_args *arg;
	ret* erati;
FSa
_OK)xs)->n eratisn+= tva4_FSt
 */
sta_MAXSERVERSpages(		i char **strii32_)	aned page argsus	retfirst %ur. "%ur eratisn"2_)	a	"ions arraym, gs)
{
	s %ugs->2_)	a		astbytewrxdrrrrrtva4_FSt
 */
sta_MAXSERVERSrxdrrrrrm, to ->ngs)
{
	ste32_)	tum, (in_rls)->n eratis; in<nm; i++pages((		i char **strilocatiofs4_R_QUAuct atiofs4ncup(p++)ine(struct xdr_streamrgs, &loc, &uct !p)))	))
		goto out_oncup(p+!= 0h(xdr,dr,;
	*len =eio;at)ioste,r,bed k_cpSoste,r erati*+)&ls)-> eratis[ls)->n eratis]}


4ncup(p++)ine(struct xdr_streamrgs, & erati->loc, & erati->uct !p)))	
		goto out_oncup(p+!= 0h(xdr,d;
	*len =eio;at)ined page ar ", 0erati->uct !p)))}es(ncup(p++)ine(str],
	qst *rgs, &ls)->;
	e{,
	u;
))
		goto out_oncup(p+!= 0h(xdr,;
	*len =eio;at}
S_OK)to ->ngs)
{
	str!_reflowncup(p++)tvalst
 *ORTED
V4t
 */
stat;rint:vened page argsfargs)
{
	strd	ne, erroa++)args->lastbytewri0;
out_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

int_eioSerncup(p++)se;
}

;
	*len prdecode_op_nd_hdr(struTR4_Wmaxfilesat m *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->bit)&P(ORTED_ATTRS|
MAX	ILEas(&ati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
MAX	ILEas(&hpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->bit)&= ~ORTED_ATTRS|
MAX	ILEas(&;at}vaned page argsmaxfilesat =%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wmaxqst *req, stunsigned int *len,	p = xdr*xdr_buen,	p = xdr*maxqst _nd_h2_t opnum;
	stri *up(p++)0;
FSnmaxqst *+)r_tw
		goto out_oargs->bit)&P(ORTED_ATTRS|
MAX   Fati1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
MAX   F)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnmaxqst *+)	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
MAX   F;at}vaned page argsmaxqst =%ugs->lastbytewr *maxqst _overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wmaxqst *req,
		unsigned int *len,	p = xdr*xdr_buen,	p = xdr*maxqst _nd_h2_t opnum;
	stri *up(p++)0;
FSnmaxqst n+)r024_tw
		goto out_oargs->bit)&P(ORTED_ATTRS|
MAXstatiti1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
MAXstat)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnmaxqst n+)	if (nfserr == NFS
args->bit)&= ~ORTED_ATTRS|
MAXstat;at}vaned page argsmaxqst =%ugs->lastbytewr *maxqst _overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wmaxqst *req, stunsigned int *len,	p = xdr*xdr_buen,	p = xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_rr024_tw
		goto out_oargs->bit)&P(ORTED_ATTRS|
MAXs(&hiti1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
MAXs(&h)pages(,	p 64xdrmaxqst }


code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);&maxqst !p)))
		gmaxqst )> 0x7FFFFFFFflowwmaxqst )= 0x7FFFFFFF;cpSnct c_r(,	p = xd)maxqst }


args->bit)&= ~ORTED_ATTRS|
MAXs(&hdr)}vaned page argsmaxqst =%lugs->lastbytewr gs->range.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wmaxrqst *req, stunsigned int *len,	p = xdr*xdr_buen,	p = xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_rr024_tw
		goto out_oargs->bit)&P(ORTED_ATTRS|
MAX
statiti1U)stat_to_errnse;
}

_OK)xpectedargs->bit)&PORTED_ATTRS|
MAX
stat)pages(,	p 64xdrmaxrqst }


code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);&maxrqst !p)))
		gmaxrqst )> 0x7FFFFFFFflowwmaxrqst )= 0x7FFFFFFF;cpSnct c_r(,	p = xd)maxrqst }


args->bit)&= ~ORTED_ATTRS|
MAX>bitmask}vaned page argsmaxrqst =%lugs->lastbytewr gs->range.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wm(unlreq, stunsigned int *len,	p = xdr*xdr_buen,mxdr,  *mode_nd_h,	p = xdrtmpatixdr_inline_	strie_os_retvFSnmodeV_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_MODtiti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_MODt)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpStmpn+)	if (nfserr == NFS
nmodeV_rtmpn& ~S_IFMT}


args->b1t)&= ~ORTED_ATTRS1_MOD&;at_to_odetvalst
 *ORTED
MOD&;at}vaned page argsfile;mode=0%ogs->lastbytewrigs->range. pa)*mode_;FStk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wnqst *req, stunsigned int *len,	p = xdr*xdr_buen,	p = xdr*nqst _nd_h2_t opnum;
	strie_os_retvFSnnqst *+)r_tw
		goto out_oargs->b1t)&P(ORTED_ATTRS1_NU	   FSiti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_NU	   FS)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnnqst *+)	if (nfserr == NFS
args->b1t)&= ~ORTED_ATTRS1_NU	   FS;at_to_odetvalst
 *ORTED
N   F;at}vaned page argsnqst =%ugs->lastbytewr gs->range. pa)*nqst _overflow:
 -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wowneam *xdr, enum nfs_opnum4 e,	p = xdr*xdr_bue
s4_statfs_arg *arg*args)
t* erati, kuidxdr*uid,es(nce_args *arg;
	ret*ownea_qst _nd_h,	p = xdrlocatixdr_inline_	strie_os_retvFSnurgV_rmake_kuid(&init_usea_qs, -2 NFS_OK)oto out_oargs->b1t)&P(ORTED_ATTRS1_OWNERoti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_OWNER)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlocs_r	if (nfserr == NFS
code(xdr, 4);
	if (unlikely= be32_)
		goto out_overfloww;
	*len = be32_to_cpS
		gownea_qst  !detULLfages(xownea_qst ->uct odekmemdr ==ly= b, GFP_NOWAIT!p)))	
		gownea_qst ->uct o!detULLfages(xxownea_qst ->locs_rlocatiofsto_odetvalst
 *ORTED
OWNER_stat;at))}es(_putfh(_OK)xoc)<>taglMAX_NETOBJfages(x_OK)
		Wmap
{
	stnfsuid( erati, XDR_QUADLly= b, uid)c__reflowfsto_odetvalst
 *ORTED
OWNER;at))t32(xdr	aned page args
		Wmap
{
	stnfsuid failed!gs->2_)	a		astbytew!p)))}nt32(xdraned page args
st  too.leng.(%u)!gs->2_)	a	return -EI= be32_)args->b1t)&= ~ORTED_ATTRS1_OWNER;at}vaned page argsurg=%rgs->lastbytewri( pa)from_kuid(&init_usea_qs, nurg)_overflow:
 -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wgrouum *xdr, unsigned int *len,	p = xdr*xdr_bue
s4_statfs_arg *arg*args)
t* erati, kgidxdr*gid,es(nce_args *arg;
	ret*grouu_qst _nd_h,	p = xdrlocatixdr_inline_	strie_os_retvFSngrgV_rmake_kgid(&init_usea_qs, -2 NFS_OK)oto out_oargs->b1t)&P(ORTED_ATTRS1_OWNER_GROUPoti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_OWNER_GROUP)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlocs_r	if (nfserr == NFS
code(xdr, 4);
	if (unlikely= be32_)
		goto out_overfloww;
	*len = be32_to_cpS
		ggrouu_qst o!detULLfages(xgrouu_qst ->uct odekmemdr ==ly= b, GFP_NOWAIT!p)))	
		ggrouu_qst ->uct o!detULLfages(xxgrouu_qst ->locs_rlocatiofsto_odetvalst
 *ORTED
GROUP_stat;at))}es(_putfh(_OK)xoc)<>taglMAX_NETOBJfages(x_OK)
		Wmap
grouu_nfsgid( erati, XDR_QUADLly= b, gid)c__reflowfsto_odetvalst
 *ORTED
GROUP;at))t32(xdr	aned page args
		Wmap
grouu_nfsgid failed!gs->2_)	a		astbytew!p)))}nt32(xdraned page args
st  too.leng.(%u)!gs->2_)	a	return -EI= be32_)args->b1t)&= ~ORTED_ATTRS1_OWNER
GROUP;at}vaned page argsgrg=%rgs->lastbytewri( pa)from_kgid(&init_usea_qs, ngrg)_overflow:
 -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wrdev*req, stunsigned int *len,	p = xdr*xdr_buendevxdr*rdev_nd_h,	p = xdrmajorc_re, m->seV_retvaxdr_inline_	strie_os_retvFSnrdevs_rMKDEV(0,0 NFS_OK)oto out_oargs->b1t)&P(ORTED_ATTRS1_RAWDEVoti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_RAWDEV)pages(devxdrtmpat


code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpSmajorc_r

	if (unlikely(opnupm->seV_r	if (nfserr == NFS
tmpn+)MKDEV(majorr m->se NFS

		gMAJOR(tmp)c__rmajorc&& MINOR(tmp)c__rm->se lowwnrdevs_rtmpati)args->b1t)&= ~PORTED_ATTRS1_RAWDEV;at_to_odetvalst
 *ORTED
RDEV;at}vaned page argsrdev=(0x%x:0x%x)gs->lastbytewr majorr m->se NFStk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Whpa	strvailm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_SPACElsVAILati1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_SPACElsVAILhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->b1t)&= ~ORTED_ATTRS1_SPACElsVAIL;at}vaned page argshpa	s avail=%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Whpa	st argm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_SPACElest
ati1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_SPACElest
hpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->b1t)&= ~ORTED_ATTRS1_SPACElest
;at}vaned page argshpa	s  arg=%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Whpa	sttotalm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*res_nd_h2_t opnum;
	stri *up(p++)0;
FSnct c_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_SPACElTOTALati1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_SPACElTOTALhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ove
args->b1t)&= ~ORTED_ATTRS1_SPACElTOTAL;at}vaned page argshpa	s total=%Lugs->lastbytewr gs->range.leng.lengt*res_overflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Whpa	stusegm *xdr, unsigned int *len,	p = xdr*xdr_buen,	p 64xdr*useg_nd_h2_t opnum;
	strie_os_retvFSnusegc_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_SPACElUSEDati1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_SPACElUSEDhpages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);useg_ove
args->b1t)&= ~ORTED_ATTRS1_SPACElUSED;at_to_odetvalst
 *ORTED
SPACElUSED;at}vaned page argshpa	s useg=%Lugs->lastbytewrxdrrgs->range.leng.lengt*useg_ovetk("nf= -
print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_Wqst *req,
		unsigned int *lenreq,
		qst spec *qst uint32_t opnum;
	int364xdrsec;_h,	p = xdrnsec;_decode(xdr, 8);
	if (unlikely12 NFS_OK)oto out_overflow;
	*len = be32_to_cpcode(xdr+ 1;
	enUINT64);&sec NFSnsecV_r	if (nfserr == NFSqst ->tv_secV_r({
	stt)sec;_hqst ->tv_nsecV_r(lengtnsec;_erflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_W{
	strcces *req,
		unsigned int *len,	p = xdr*xdr_buen *clp)
qst spec *qst uint3stri *up(p++)0;
FSqst ->tv_secV_r0;_hqst ->tv_nsecV_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_comp_ACCESSiti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_comp_ACCESS)pages(ncup(p++)ine(strTR4_Wqst * *lenqst up)))
		gncup(p++_reflowfncup(p++)tvalst
 *ORTED
Acomp;at
args->b1t)&= ~ORTED_ATTRS1_comp_ACCESS;at}vaned page argsaqst =%lrgs->lastbytewri(lengtqst ->tv_sec_overflow:
 *up(pprdecode_op_nd_hdr(struTR4_W{
	stmetauct *req,
		unsigned int *len,	p = xdr*xdr_buen *clp)
qst spec *qst uint3stri *up(p++)0;
FSqst ->tv_secV_r0;_hqst ->tv_nsecV_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_comp_METADATAiti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_comp_METADATA)pages(ncup(p++)ine(strTR4_Wqst * *lenqst up)))
		gncup(p++_reflowfncup(p++)tvalst
 *ORTED
Ccomp;at
args->b1t)&= ~ORTED_ATTRS1_comp_METADATA;at}vaned page argscqst =%lrgs->lastbytewri(lengtqst ->tv_sec_overflow:
 *up(pprdecode_op_nd_hdr(struTR4_W{
	stdelt *req,
		unsigned int *len,	p = xdr*xdr_bue2_)	a n *clp)
qst spec *qst uint3stri *up(p++)0;
FSqst ->tv_secV_r0;_hqst ->tv_nsecV_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_comp_DELTAiti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_comp_DELTA)pages(ncup(p++)ine(strTR4_Wqst * *lenqst up)))args->b1t)&= ~ORTED_ATTRS1_comp_DELTA;at}vaned page args{
	stdelt =%lr %lrgs->lastbytewri(lengtqst ->tv_sece2_)(lengtqst ->tv_nsec_overflow:
 *up(pprdecode_op_nd_hdr(struTR4_Wsecurity_labelm *xdr, unsigned int *len,	p = xdr*xdr_bue
iofs4nceturn_args *bel * *bel_nd_h,	p = xdrpiV_retva,	p = xdrlf c_retva__3] = {catixdr_inline_	stri *up(p++)0;
FS
		goto out_oargs->b2t)&P(ORTED_ATTRS2_SECURITY_LABELati1U)stat_to_errnse;
}

_OK)xpectedargs->b2t)&PORTED_ATTRS2_SECURITY_LABEL)pages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlfp++);
	hdr->taglen = be3(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSpiV_r;
	hdr->taglen = be3(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlocs_r	if (nfserr == = be3(code(xdr, 4);
	if (unlikely= be32_)
		goto out_overfloww;
	*len = be32_to_cpS
		gxoc)<>tva4_MAX ABELLENfages(x_OK) *bel_ages(xxmemcpyg *bel
	enbelcoply= be32_)		 *bel
	eocs_rlocatiofs *bel
	piV_rpi32_)		 *bel
	efp++)efp32_)		ncup(p++)tvalst
 *ORTED
V4tSECURITY_LABEL;at))}es(	
	encod2]s&= ~ORTED_ATTRS2_SECURITY_LABELp)))}nt32(xdraed pageKERN_WARNING  argsr*bel too.leng.(%u)!gs->2_)	a	return -EI= be32_}
S_OK)r*bel &&  *bel
	enbelflowned page argsrnbel=%sEI= b=%d, PI=%d, LFS=%rgs->lastbytewrxdrrgDR_QUAD *bel
	enbelco *bel
	eocco *bel
	pico *bel
	efs_overflow:
 *up(pprrint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTR4_W{
	stmodify*req,
		unsigned int *len,	p = xdr*xdr_buen *clp)
qst spec *qst uint3stri *up(p++)0;
FSqst ->tv_secV_r0;_hqst ->tv_nsecV_retva
		goto out_oargs->b1t)&P(ORTED_ATTRS1_comp_MODIFYiti1U)stat_to_errnse;
}

_OK)xpectedargs->b1t)&PORTED_ATTRS1_comp_MODIFY)pages(ncup(p++)ine(strTR4_Wqst * *lenqst up)))
		gncup(p++_reflowfncup(p++)tvalst
 *ORTED
Mcomp;at
args->b1t)&= ~ORTED_ATTRS1_comp_MODIFY;at}vaned page argsmqst =%lrgs->lastbytewri(lengtqst ->tv_sec_overflow:
 *up(pprdecode_op_nd_hc__ifyuTR4_Wds.m *xdr, unsigned int *len, char **stri;
	eoen,	p = xdrTR4_loc_nd_h, char **striTR4_, xdr ->taglen);
	hdTR4_loc_;_h, char **strin, xdr ->(unsigned i_poask, _iti;
	eou >> 2;
FS
		goto out_oTR4_, xdr != n, xdr)pages(ded page argsseration"
		" %dincorrectVtatic voirds.\n":n"2_)	"%u %c %ugs->2_)	aastbytewrxdrrrTR4_, xdr layout.drrgTR4_, xdr l n, xdr) ? '<' : '>'ut.drrn, xdr layo);at_to_errnse;
}

sterflow:
	prode_compound_hdr(strucs, &h_ arg*req,
		unsigned int *lentreturn_argscs, &h_ arg *, argund_h2_t opnum;
decode(xdr, 4);
	if (unlikely20 NFS_OK)oto out_overflow;
	*len = be32_to_cp, arg->atomicV_r	if (nfserr == = be3code(xdr+ 1;
	enUINT64);&, arg->befor up))(xdr+ 1;
	enUINT64);&, arg->afte _overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struTcces *req,
		unsigned int *len,opnusupporneren,opnuTcces uint32_t opnum;
	int32_t nsupp, Tcce_	stri *up(p;
dencup(p++)ine(strucd, &operatOP_ACCESS)NFS_OK)0;
out_at_to_errn *up(p;
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_csuppV_r	if (nfserr == = be3TccV_r	if (nfserr == NFSusuppornerV_rsuppNFSuTcces V_rTcce_	rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struuct xdrfixegm *xdr, unsigned int *lenow_ms*buf);hat xdrlocund_h2_t opnum;
decode(xdr, 4);
	if (unlikelyly(!p))
		gxpectedpstages(memcpygbuf);ply= be32_)rflow:
	pr
steg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struc_rqst *req,
		unsigned int *leno(nfserr)ergV*err)ergund_hro_errnine(struct xdrfixegm *lentrr)erg,>tva4_*/
stat
ps(&hprode_compound_hdr(struclos *req,
		unsigned int *lentreturn_aruclos ct c*res_nd_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_CLOSE)NFS_OK)0;
out != ool flow_aruincreme_funpenrgs-t *re
out, to ->gs-t )NFS_OK)!0;
out_at_ncup(p++)ine(strc_rqst *xgs, &to ->err)erguoverflow:
 *up(pprdecode_op_nd_hdr(struc__ifieam *xdr, enum nfs_opnum4 eow_ms*c__ifieaund_hro_errnine(struct xdrfixegm *lenc__ifiea,>tva4_VERIFIER
ps(&hprode_compound_hdr(strurqst uc__ifieam *xdr, enum nfs_opnum4 etreturn_arurqst uc__ifieas*c__ifieaund_hro_errnine(struct xdrfixegm *lenc__ifiea->uct cotva4_VERIFIER
ps(&hprode_compound_hdr(strucomminm *xdr, unsigned int *lentreturn_arucomminct c*res_nd_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_COMMIT!p))_OK)!0;
out_at_ncup(p++)ine(strrqst uc__ifieamxgs, &to ->c__,->c__ifieauoverflow:
 *up(pprdecode_op_nd_hdr(strucreat *req, stunsigned int *lentreturn_argscs, &h_ arg *, argund_h2_t opnum;
h,	p = xdrbmlocatind_h *up(p;
dencup(p++)ine(strucd, &operatOP_CRE
st)NFS_OK)0;
out_at_to_errn *up(p;
e_OK)(ncup(p++)ine(strcs, &h_ arg*perat, argu)_at_to_errn *up(p;
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpbmloc);

	p = xdr_inline_ecode(xdr, 8);
	if (unlikelybmloc)layo)p))
		gxpectedpst2_)rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struserati_cap *req,
		unsigned int *lennce_args *argerati_cap _ct c*res_nd_h, char **stri;
	eo;
h,	p = xdrTR4_loc, args->b3]s_r{0}atind_h *up(p;
de_OK)(ncup(p++)ine(strucd, &operatOP_GETRTEDsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wsuppornerm *lenxdr_bp, to ->TR4_Wxdr_bskuta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wfh_i_
ireWu_tom *lenxdr_bp,2_)	a		 &to ->sh_i_
ireWu_tosta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wdst rsuppornm *lenxdr_bp, &to ->hasWdst suta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Ws_rqst rsuppornm *lenxdr_bp, &to ->hasWs_rqst suta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Waclsuppornm *lenxdr_bp, &to ->aclWxdr_bskuta!_reflow;
	*lunsierroa;
encup(p++)c__ifyuTR4_Wds.m *lenn
	eoenTR4_loc_;_unsierroa:vened page argsunson"
		" %d%d!gs->lastbytewri-0;
out_overflow:
 *up(pprdecode_op_nd_hdr(struc_rqf *req,
		unsigned int *lennce_args *_fsc_rq **ac_rq_nd_h, char **stri;
	eo;
h,	p = xdrTR4_loc, args->b3]s_r{0}atind_h *up(p;
de_OK)(ncup(p++)ine(strucd, &operatOP_GETRTEDsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouta!_reflow;
	*lunsierroa;

e_OK)(ncup(p++)ine(strTR4_Wfiles_availm *lenxdr_bp, &*ac_rq->afilesuta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wfiles_ argm *lenxdr_bp, &*ac_rq->ffilesuta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wfiles_totalm *lenxdr_bp, &*ac_rq->tfilesuta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Whpa	strvailm *lenxdr_bp, &*ac_rq->abytesuta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Whpa	st argm *lenxdr_bp, &*ac_rq->fbytesuta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Whpa	sttotalm *lenxdr_bp, &*ac_rq->tbytesuta!_reflow;
	*lunsierroa;

encup(p++)c__ifyuTR4_Wds.m *lenn
	eoenTR4_loc_;_unsierroa:vened page argsunson"
		" %d%d!gs->lastbytewri-0;
out_overflow:
 *up(pprdecode_op_nd_hdr(stru{,
	conf*req,
		unsigned int *lennce_args *_{,
	confnum,
	conf_nd_h, char **stri;
	eo;
h,	p = xdrTR4_loc, args->b3]s_r{0}atind_h *up(p;
de_OK)(ncup(p++)ine(strucd, &operatOP_GETRTEDsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouta!_reflow;
	*lunsierroa;

e_OK)(ncup(p++)ine(strTR4_Wmaxqst * *lenxdr_bp, &m,
	conf->maxWdst sta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wmaxqst * *lenxdr_bp, &m,
	conf->maxWqst loc_ta!_reflow;
	*lunsierroa;

encup(p++)c__ifyuTR4_Wds.m *lenn
	eoenTR4_loc_;_unsierroa:vened page argsunson"
		" %d%d!gs->lastbytewri-0;
out_overflow:
 *up(pprdecode_op_nd_hdr(struthto hold_hnd_m *xdr, unsigned int *le nfs41_,	p = xdr*xdr_bue2_)	a n,	p 64xdr*rese nfs41_,	p = xdrhnd_Wxdr_nd_h2_t opnum;
FSnct c_retva
		gxpectedargs->bit)&Phnd_Wxdr_pages(code(xdr, 4);
	if (unlikely8!p)))
		goto out_overfloww;
	*len = be32_to_cpS(xdr+ 1;
	enUINT64);res_ovesterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strufirstuthto hold_st m4m *xdr, unsigned int *le nfs44nceturn_argsthto holdr*res_nd_h2_t opnum;
	, char **stri;
	eo;
h,	p = xdrargs->b3]s_r{0,}enTR4_locatind_h *up(p;
de/* layintS_SERVges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overfages(c(__func__, xdr);
	return -EIO;
}

s_to_errnse;
}

sterf ->g__SERV_r	if (nfserr == NFde/* thi_hnd_sedrargs->Vges(ncup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsNFS_OK)0;
out <reflow;
	*lunsierroa;

e/* thi_hnd_listrds.\n"Vges(ncup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouNFS_OK)0;
out <reflow;
	*lunsierroa;
e/* thi_hnd_listrges(ncup(p++)ine(strthto hold_hnd_m *lenxdr_bp, &to ->rd_sz, THRESHOLD_RDuNFS_OK)0;
out <reflow;
	*lunsierroa;
encup(p++)ine(strthto hold_hnd_m *lenxdr_bp, &to ->wr_sz, THRESHOLD_WRuNFS_OK)0;
out <reflow;
	*lunsierroa;
encup(p++)ine(strthto hold_hnd_m *lenxdr_bp, &to ->rd_io_sz, nfs41______THRESHOLD_RD_IOuNFS_OK)0;
out <reflow;
	*lunsierroa;
encup(p++)ine(strthto hold_hnd_m *lenxdr_bp, &to ->wr_io_sz, nfs41______THRESHOLD_WR_IOuNFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)c__ifyuTR4_Wds.m *lenn
	eoenTR4_loc_;_erf ->bp++);rgs->bit;

ened page ar bm=0x%x rd_sz=%llu wr_sz=%llu rd_io=%llu wr_io=%llugs->2_)lastbytewrirf ->bprirf ->rd_sz, to ->wr_sz, to ->rd_io_sz, nfto ->wr_io_sz_;_unsierroa:vened page aron"
=%d!gs->lastbytewri0;
out_overflow:
 *up(pprdec/*
 * Thto holds on pNFS directVI/O vrs MDSVI/O
rgesode_op_nd_hdr(struTR4_Wmdsthto holdm *xdr, unsigned int *le nfs41_te,	p = xdr*xdr_bue2_)	a n  nceturn_argsthto holdr*res_nd_h2_t opnum;
	stri *up(p++)0;
h,	p = xdrnum;
FS
		goto out_oargs->b2t)&P(ORTED_ATTRS2_MDSTHRESHOLDiti1U)stat_to_errnse;
}

_OK)args->b2t)&PORTED_ATTRS2_MDSTHRESHOLDfages(/* Did thesseration"
		" a bun,
			op%d\nR4_c voi?rges(S
		goto out_oto c__rtULLftat__to_errnseREMOTe;
}


code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSnumV_r	if (nfserr == NFS
_OK)
umV__reflowfrflow:
	pr

_OK)
umV> 1)xdraed pageKERN_INFO  argsWarning: Multiple pNFS layintS"2_)	a"driatisnINTsfilesyop%m not suppornergs->2_)	aastbytew NFdeencup(p++)ine(strfirstuthto hold_st m4mikelyres_ove
args->b2]s&= ~ORTED_ATTRS2_MDSTHRESHOLDovesterflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strugetfTR4_WaR4_sm *xdr, unsigned int *len,	p = xdr*xdr_bue
ionce_args *_faR4_ *faR4_en *clp)
{
	s*h **h,es(nce_args *ar*args)
{
	str**args)ennce_args *ar *bel * *bele
s4_statfs_arg *arg*args)
t* erati_nd_hnd_h *up(p;
	,mxdr,  fmodeV_retva,	p = xdrtSER;
	str= xdrerr;
dencup(p++)ine(strTR4_Wq_tom *lenxdr_bp, &u_tosNFS_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->modeV_retva
		g0;
out != 0fages(faR4_->modeV|=*arg*u_to2fmt[u_to]}


faR4_->valid |=* *up(p;
	}
dencup(p++)ine(strTR4_Wcs, &hm *lenxdr_bp, &*aR4_->cs, &h_aR4_sNFS_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wsat m *lenxdr_bp, &*aR4_->hat _;dr_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_W*argm *lenxdr_bp, &*aR4_->*arg_;dr_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
deerrV_retvancup(p++)ine(strTR4_Werroam *lenxdr_bp, &erruNFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)ine(strTR4_Wfilencode_m *lenxdr_bp, fhuNFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)ine(strTR4_Wfilergm *lenxdr_bp, &*aR4_->*ilerg_NFS_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_W*a rpc_rqst * *lenxdr_bp, fa rpc_NFS_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wm(unlikelybdr_bp, &*mode_;FS_OK)0;
out <reflow;
	*lunsierroa;
e
		g0;
out != 0fages(faR4_->modeV|=**mode}


faR4_->valid |=* *up(p;
	}
dencup(p++)ine(strTR4_Wnqst * *lenxdr_bp, &faR4_->nqst _ove_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wowneam *lenxdr_bp,  erati, &faR4_->uid, faR4_->ownea_qst _ove_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wgrouum *lenxdr_bp,  erati, &faR4_->gid, faR4_->grouu_qst _ove_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wrdev* *lenxdr_bp, &faR4_->rdev_NFS_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wspa	stusegm *lenxdr_bp, &faR4_->du.arg3.useg_ove_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_W{
	strcces * *lenxdr_bp, &faR4_->aqst up))_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_W{
	stmetauct * *lenxdr_bp, &*aR4_->cqst up))_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_W{
	stmodify* *lenxdr_bp, &*aR4_->mqst up))_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wmcv_bed_onWfilergm *lenxdr_bp, &*aR4_->mcv_bed_onWfilergup))_OK)0;
out <reflow;
	*lunsierroa;
efaR4_->valid |=* *up(p;
dencup(p++)ine(strTR4_Wmdsthto holdm *lenxdr_bp, faR4_->mdsthto holduNFS_OK)0;
out <reflow;
	*lunsierroa;

e_OK) *bel_ages(ncup(p++)ine(strTR4_Wsecurity_labelm *lenxdr_bp,  *bel_pr

_OK)0;
out <refloww;
	*lunsierroa;
e
faR4_->valid |=* *up(p;
	}
dunsierroa:vened page argsunson"
		" %d%dgs->lastbytewri-0;
out_overflow:
 *up(pprdecode_op_nd_hdr(strugetfTR4_Wgeneaic*req,
		unsigned int *lennce_args *_faR4_ *faR4_e
ionce_args *_fh **h,n *clp)
{
	g *args)
{
	str**args)e
s4nceturn_args *bel * *bel, )
tatfs_arg *arg*args)
t* erati_nd_h, char **stri;
	eo;
h,	p = xdrTR4_loc,2_)largs->b3]s_r{0}atind_h *up(p;
dencup(p++)ine(strucd, &operatOP_GETRTEDsNFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsNFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouNFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)ine(strgetfTR4_WaR4_sm *lenxdr_bp, faR4_, fh, fa rpce nfs44 *bel,  erati_NFS_OK)0;
out <reflow;
	*lunsierroa;

encup(p++)c__ifyuTR4_Wds.m *lenn
	eoenTR4_loc_;_unsierroa:vened page argsunson"
		" %d%dgs->lastbytewri-0;
out_overflow:
 *up(pprdecode_op_nd_hdr(strugetfTR4_Wlabelm *xdr, unsigned int *lennce_args *_faR4_ *faR4_e
ionce_args *gs *bel * *bel, )
tatfs_arg *arg*args)
t* erati_nd_hro_errnine(strgetfTR4_Wgeneaic* *lenfaR4_, tULL, tULL,  *bel,  erati_NFdecode_op_nd_hdr(strugetfTR4_m *xdr, unsigned int *lennce_args *_faR4_ *faR4_e
io)
tatfs_arg *arg*args)
t* erati_nd_hro_errnine(strgetfTR4_Wgeneaic* *lenfaR4_, tULL, tULL, tULL,  erati_NFdec/*
 * Dne(st potentially multiple layintS_SERs. Currently we only supporn
 * one layintSdriatinINTsfile syop%m.
rgesode_op_nd_hdr(strufirstuparg*layintWq_tom *xdr, unsigned int *le nfs44n,	p = xdr*layintu_tosnd_h2_t opnum;
	strinum;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpcumV_r	if (nfserr == NF
e/* pNFS is not supporner by thesunderly	retfile syop%mages(
		g
umV__refages(*layintu_toV_retva)rflow:
	pr
ste_OK)
umV> 1)xdred pageKERN_INFO  NFS: argsWarning: Multiple pNFS layintS"2_)	"driatisnINTsfilesyop%m not suppornergs-> astbytew NFde/* Dne(st cod sedrfirst layintS_SER, m bee(xd->p past unusegc_SERsVges(]ode(xdr, 8);
	if (unlikely
umV*y(!p))
		goto out_overflow;
	*len = be32_to_cp*layintu_toV_r	if (nfserr == NFSrflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode/*
 * TheS_SERVoftfile syop%maexporner.
rg Note we must ensuren bitmlayintu_toVis set*/
	any non-erroa+case.
rgesode_op_nd_hdr(struTR4_Wpargq_tom *xdr, unsigned int *lee,	p = xdr*xdr_bue2_)	a,	p = xdr*layintu_tosnd_hstri *up(p++)0;
FSned page argsargs->Vis %xgs->lastbytewriargs->b1t!p))
		goto out_oargs->b1t)&P(ORTED_ATTRS1_FSt
AYOUT_TYPESiti1U)stat_to_errnse;
}

_OK)args->b1t)&PORTED_ATTRS1_FSt
AYOUT_TYPES_ages(ncup(p++)ine(strfirstuparg*layintWq_tom *leelayintu_tosove
args->b1t)&= ~ORTED_ATTRS1_FSt
AYOUT_TYPESpr
snt32(xdr*layintu_toV_retvarflow:
 *up(pprdec/*
 * The preferer brpck;hat aym, gayintSdirect **so
rgesode_op_nd_hdr(struTR4_WlayintWblksat m *xdr, unsigned int *len,	p = xdr*xdr_bue nfs41_____,	p = xdr*res_nd_h2_t opnum;
FSned page argsargs->Vis %xgs->lastbytewriargs->b2t!p))nct c_retva
		gargs->b2t)&PORTED_ATTRS2_
AYOUT_BLKps(&hages(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfages(xc(__func__, xdr);
	return -EIO;
}

s__to_errnse;
}

)}es(nct c_r	if (nfserr == NFS
args->b2]s&= ~ORTED_ATTRS2_
AYOUT_BLKps(&ovesterflow:
	prode_compound_hdr(strufs arg*req,
		unsigned int *lentreturn_arufs argr**a argund_h, char **stri;
	eo;
h,	p = xdrTR4_loc, args->b3]atind_h *up(p;
de_OK)(ncup(p++)ine(strucd, &operatOP_GETRTEDsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouta!_reflow;
	*lunsierroa;

e*a arg->rtmult++)*a arg->wtmult++)512;e/* ???rges
e_OK)(ncup(p++)ine(strTR4_WdsaseWqst * *lenxdr_bp, &*a arg->dsaseWqst sta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wmaxfilesat m *lenxdr_bp, &*a arg->maxfilesat sta!_reflow;
	*lunsierroa;
e_OK)(ncup(p++)ine(strTR4_Wmaxqst * *lenxdr_bp, &*a arg->rtmaxsta!_reflow;
	*lunsierroa;
e*a arg->rtpref++)*a arg->dtpref++)*a arg->rtmax;
e_OK)(ncup(p++)ine(strTR4_Wmaxrqst * *lenxdr_bp, &*a arg->wtmaxsta!_reflow;
	*lunsierroa;
e*a arg->wtpref++)*a arg->wtmax;
encup(p++)ine(strTR4_W{
	stdelt * *lenxdr_bp, &*a arg->{
	stdelt )NFS_OK)0;
out != eflow;
	*lunsierroa;
encup(p++)ine(strTR4_Wpargq_tom *lenxdr_bp, &*a arg->dayintu_tosove_OK)0;
out != eflow;
	*lunsierroa;
encup(p++)ine(strTR4_WlayintWblksat m *lenxdr_bp, &*a arg->blksat )NFS_OK)0;
out_at_;
	*lunsierroa;

encup(p++)c__ifyuTR4_Wds.m *lenn
	eoenTR4_loc_;_unsierroa:vened page argsunson"
		" %d%d!gs->lastbytewri-0;
out_overflow:
 *up(pprdecode_op_nd_hdr(strugetfh*req,
		unsigned int *lentreturn_arufh **hund_h2_t opnum;
h,	p = xdrlocatind_h *up(p;
de/* Zero ncode_rfirst 	*lallow )
{
aris	str*es(memset(fh, 0);hat of(**hu);
dencup(p++)ine(strucd, &operatOP_GETFH)NFS_OK)0;
out_at_to_errn *up(p;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cploc);

	p = xdr_inline_e
		gxoc)> tva4_FHps(&hat_to_errnse;
}

fh->hat  _rlocaticode(xdr, 4);
	if (unlikelyly(!p))
		goto out_overflow;
	*len = be32_to_cpmemcpygfh->uct coply= be32_rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struqst *req, stunsigned int *lentreturn_argscs, &h_ arg *, argund_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_   F)NFS_OK)0;
out_at_to_errn *up(p;
hro_errnine(strcs, &h_ arg*perat, arguprdec/*
 * We creat  thesowneaento we know a proINTsownea.idrds.\n"Vis 4.
rgesode_op_nd_hdr(strurpck	ifni %d*req, stunsigned int *lentreturnfileurpck **l_nd_h,	p 64xdroffset,rds.\n"at,lienti }

2_t opnum;
h,	p = xdrqst loc,rtSER;
ticode(xdr, 4);
	if (unlikely32); /* qst )opnbytesages(
		goto out_overflow;
	*len = be32_to_cpcode(xdr+ 1;
	enUINT64);&offset); /* qst )2 8-byte.leng., xdr ges(]ode(xdr+ 1;
	enUINT64);&ds.\n")NFSu_toV_r	if (nfserr ==++); /* 4nbyte qst )ges(
		gflo!detULLfag /* manipulat  file rpck *es(Sfl->fluc_rrt++)(loff_t)offset;s(Sfl->fluenrV_rfl->fluc_rrt++)(loff_t)ds.\n"V-)r_twe
		gxoc\n"V== ~(,	p 64xd)eflowwfl->fluenrV_rOFFSET_MAX;s(Sfl->fluu_toV_rF_WRLCK_twe
		gu_toV& 1)xdrafl->fluu_toV_rF_RDLCK_twefl->flupigc_retva}e3code(xdr+ 1;
	enUINT64);&,lienti ); /* qst )8nbytesages(qst loc);

	p = xdr_inline /* qst )4nbytesage  /* h
	e qst )all)opnbytesanow ges(]ode(xdr, 8);
	if (unlikely
st loc_e /* variable sat ayiel )ges(
		gxpectedpst2_)rflow:
-tva4ERR_DENIED;aint_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struqpck*req,
		unsigned int *lentreturn_arurpck	ct c*res_nd_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_LOCF)NFS_OK)0;
outV== ool flow;
	*len pr	
		gncup(p++_refages(ncup(p++)ine(strncupst *xgs, &to ->err)erguove(
		goto out_o0;
out_floww;
	*len pr
snt32( _OK)0;
outV== otva4ERR_DENIED_at_ncup(p++)ine(strrpck	ifni %*xgs, tULLfpr	
		gto ->npenrgs-t o!detULLf
ow_aruincreme_funpenrgs-t *re
out, to ->npenrgs-t fpr	_aruincreme_furpck	gs-t *re
out, to ->rpck	gs-t );rint:verflow:
 *up(pprdecode_op_nd_hdr(strurpcknm *xdr, unsigned int *lentreturn_arurpckn	ct c*res_nd_hnd_h *up(p;
encup(p++)ine(strucd, &operatOP_LOCFT)NFS_OK)0;
outV== otva4ERR_DENIED_at_ro_errnine(strrpck	ifni %*xgs, to ->ifni %_overflow:
 *up(pprdecode_op_nd_hdr(strurpckum *xdr, unsigned int *lentreturn_arurpcku	ct c*res_nd_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_LOCFU)NFS_OK)0;
out != ool flow_aruincreme_furpck	gs-t *re
out, to ->gs-t )NFS_OK)ncup(p++_refes(ncup(p++)ine(strncupst *xgs, &to ->err)erguoverflow:
 *up(pprdecode_op_nd_hdr(struredsaseWrpckowneam *xdr, enum nfs_opnum4und_hro_errnine(strucd, &operatOP_RELEASE_LOCFOWNER)prdecode_op_nd_hdr(strurpokuum *xdr, unsigned int *lund_hro_errnine(strucd, &operatOP_LOOKUPuprdec/* This is too.sick!rgesode_op_nd_hdr(struspa	stliminm *xdr, unsigned int *lenu64 *maxsat )nd_h2_t opnum;
h,	p = xdrlimin__SER, nbrpckt, brpcktat ;_decode(xdr, 8);
	if (unlikely12 NFS_OK)oto out_overflow;
	*len = be32_to_cplimin__SERV_r	if (nfserr ==++);denwitch	gxpmin__SERfagescase 1:cpS(xdr+ 1;
	enUINT64);maxsat )NFS
aed k;escase 2:cpSnbrpcktV_r	if (nfserr ==++);de	brpcktat c_r	if (nfserr == NFS
*maxsat  _r(,	p 64xd)nbrpcktV*r(,	p 64xd)brpcktat ;_esterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strudeleg
{
	sm *xdr, unsigned int *lentreturn_arunpenct c*res_nd_h2_t opnum;
h,	p = xdrdeleg
{
	s_tSER;
	strn *up(p;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpdeleg
{
	s_tSER);

	p = xdr_inline_e
		gdeleg
{
	s_tSER);= tva4_OPEN_DELEGATE_NON&hages(to ->ifleg
{
	s_tSER);
etva)rflow:
	pr
stencup(p++)ine(strncupst *xgs, &to ->ifleg
{
	s!p))
		goto out_o0;
out_flowto_errn *up(p;
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpto ->iourecall)_r	if (nfserr == NF
enwitch	gifleg
{
	s_tSERfagescase tva4_OPEN_DELEGATE_s(&h:es(to ->ifleg
{
	s_tSER);
FMOD&_s(&hdr)
aed k;escase tva4_OPEN_DELEGATE_>bitm:es(to ->ifleg
{
	s_tSER);
FMOD&_>bitm|FMOD&_s(&hdr)

		gde(struspa	stliminmxgs, &to ->maxsat ) <refloww_to_errnse;
}

sterflow:
ine(strTce*xgs, tULL, to ->gsrati->_aruclient)print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struucs.m *xdr, unsigned int *lentreturn_arunpenct c*res_nd_h2_t opnum;
h,	p = xdrn
	e, xdrlybmloc, iatind_h *up(p;
de_OK)!__ine(strucd, &operatOP_OPEN, &;;
out_flowto_errn *up(p;
e_aruincreme_funpenrgs-t *re
out, to ->gs-t )NFS_OK)0;
out_at_to_errn *up(p;
hncup(p++)ine(strncupst *xgs, &to ->err)erguove
		goto out_o0;
out_flowto_errn *up(p;
cpdee(strcs, &h_ arg*perat&to ->, argupr
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cto ->rflagtV_r	if (nfserr ==++);debmloc);

	p = xdr_inline_e
		gamxoc)> 1eflow;
	*lunsierroa;

ecode(xdr, 8);
	if (unlikelybmloc)layo)p))
		goto out_overflow;
	opnum = be32_to_cs
	e, xdr _rm->_t(,	p = xdlybmloc, tva4_BITMAP
ps(&hpr	ym, (iV_ret i <rs
	e, xdr; ++i)es(to ->aR4_set[i]V_r	if (nfserr ==++);deym, (t i <rtva4_BITMAP
ps(&t i++)es(to ->aR4_set[i]V_r0;
FSrflow:
ine(strdeleg
{
	smxgs, to _;_unsierroa:vened page argsBrgs->Vtoo.large! Loc\n"V= %ugs->lastbytewr bmloc}

static bool __int_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struucs._confirmm *xdr, unsigned int *lentreturn_arunpen_confirmct c*res_nd_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_OPEN_CONFIRM)NFS_OK)0;
out != ool flow_aruincreme_funpenrgs-t *re
out, to ->gs-t )NFS_OK)!0;
out_at_ncup(p++)ine(strc_rqst *xgs, &to ->err)erguoverflow:
 *up(pprdecode_op_nd_hdr(strunpenrdowngraunlreq, stunsigned int *lentreturn_aruclos ct c*res_nd_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_OPEN_DOWNGRADE)NFS_OK)0;
out != ool flow_aruincreme_funpenrgs-t *re
out, to ->gs-t )NFS_OK)!0;
out_at_ncup(p++)ine(strc_rqst *xgs, &to ->err)erguoverflow:
 *up(pprdecode_op_nd_hdr(struputfh*req,
		unsigned int *lund_hro_errnine(strucd, &operatOP_PUTFH)NFdecode_op_nd_hdr(struputrootfh*req,
		unsigned int *lund_hro_errnine(strucd, &operatOP_PUTROOTFH)NFdecode_op_nd_hdr(struqst *req, stunsigned int *lentreturnrpc_rqsdr*req,2_)lllllllnce_args *_{gio_ct c*res_nd_h2_t opnum;
h,	p = xdrccv_b, eof, tocvdatind_h *up(p;
dencup(p++)ine(strucd, &operatOP_s(&h)NFS_OK)0;
out_at_to_errn *up(p;
hcode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_ceofV_r	if (nfserr ==++);deccv_bV_r	if (nfserr == NFSrfcvdode(xdrqst _pagesm *lenccv_b!p))
		gccv_bV> tocvdpages(ded page NFS: args)
tchee_ong*/
	qst )reply:S"2_)	a"ccv_bV%uV> tocvd %ugs->lccv_b, tocvdp;
io)
v_bV_rtocvdaticeofV_r0;

sterf ->eofV_reofo_cto ->)
v_bV_r)
v_b32_rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struqst di_m *xdr, unsigned int *lennce_argrpc_rqsdr*req,ntreturn_argsqst di__ct c*ret di__nd_hnd_t_ncup(p}

2_t op		c__,[2];
dencup(p++)ine(strucd, &operatOP_s(&hDIR)NFS_OK)!0;
out_at_ncup(p++)ine(strc__ifieamxgs, ret di_->c__ifiea.uct !p))
		goto out_o0;
out_flowto_errn *up(p;
ememcpygc__,, ret di_->c__ifiea.uct );hat of(c__,)_ovened page argsc__ifieas= %08x:%08xgs->2_)	return -EIc__,[0]EIc__,[1t!p))to_errn(xdrqst _pagesm *lenxxd->buf->page_loc}

ode_compound_hdr(struqst qst *req, stunsigned int *lentreturnrpc_rqsdr*req_nd_hreq, stunsibufr*rcvbufr= &toq->rq_rcvibuf;
h,] = {c, tocvdati2_t opnum;
	stri *up(p;
dencup(p++)ine(strucd, &operatOP_s(&h   F)NFS_OK)0;
out_at_to_errn *up(p;
de/* Conc__trds.\n"VofVs_rqst Vges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cploc);

	p = xdr_inline_e
		gxoc)>_rtcvbuf->page_loc || loc)l_refages(ned page _argsseration"
		" %dgiatri _rqst !gs-)tva)rflow:
-EstatTOOLONG;

sterfcvdode(xdrqst _pagesm *lenly(!p))
		grfcvdo<nly(!ages(ded page NFS: args)
tchee_ong*/
	qst qst Vreply:S"2_)	a"ccv_bV%uV> tocvd %ugs->l {c, tocvd)tva)rflow:
-E;
}

ste/*
	 * The XDR ene(st ren );
 has set*thongs upnto  bit
	 * thesqst Vtext will)ber)
pi %ddirectlyund_o  be
	 * buffea.  We just h
	e _o d*lebe32_to-checkong,
	 * cod cod null-term->at  thestext (thesVFSaexpects
	 * null-term->at
	s!.
	 */))(xdrterm->at rg;
	re(tcvbufly= be32_rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(struremnc_*req, stunsigned int *lentreturn_argscs, &h_ arg *, argund_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_REMOVt)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strcs, &h_ arg*perat, arguprint:verflow:
 *up(pprdecode_op_nd_hdr(strureqst *req, stunsigned int *lentreturn_argscs, &h_ arg *old_, arg,
	   n  nceturn_argscs, &h_ arg *new_, argund_hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_REstat)NFS_OK)0;
out_at_;
	*len pr
_OK)(ncup(p++)ine(strcs, &h_ arg*peratold_, arg))_at_;
	*len pr
ncup(p++)ine(strcs, &h_ arg*peratnew_, arguprint:verflow:
 *up(pprdecode_op_nd_hdr(strureqewm *xdr, enum nfs_opnum4und_hro_errnine(strucd, &operatOP_RENEW}

ode_compound_
dr(strurestorefh*req,
		unsigned int *lund_hro_errnine(strucd, &operatOP_RESTOREFH)NFdecode_op_nd_hdr(strugetaclm *xdr, unsigned int *lennce_argrpc_rqsdr*req,2_)	lnce_args *_getaclct c*res_nd_h, char **stri;
	eo;
h,	p = xdrTR4_loc,2_)largs->b3]s_r{0}atind_h *up(p;
h, char **stripg_offset;s_cto ->aclWloc);
0pr
_OK)(ncup(p++)ine(strucd, &operatOP_GETRTEDsta!_reflow;
	*len pr))(xdre_fer_pagem *lenxxd->buf->page_loc}

de/* Calculat  thesoffsetVofVthespage uct oges(]g_offsetode(xd->buf->heed[0].iov_locat
e_OK)(ncup(p++)ine(strTR4_Wxdr_bum *lenxdr_bpsta!_reflow;
	*len pr
_OK)(ncup(p++)ine(strTR4_Wds.\n"mxgs, &TR4_loc, &;
	eouta!_reflow;
	*len pr))
		goto out_oargs->b0t)&P(ORTED_ATTRS0_ACLati1U)stat_to_errnse;
}

_OK)xpectedargs->b0t)&PORTED_ATTRS0_ACLrfage
s(/* The args->Vmxgs loc)+ args->s) cod thesaR4_ xgs loc), xdr2_)l* cre stored win"Vthesacl uct oto ncode_rthesproblemVof2_)l* variable ds.\n"Vargs->s.*es(Sto ->aclWuct _offsetode(xdigned i_poask, _itipg_offset;s	cto ->aclWloc);
TR4_locat
s(/* Checkaym, recei	e buffealebe32_torges(S
		gto ->aclWloc)>Vmxgs->n, xdr layo) ||2_)llllto ->aclWloc)+ to ->aclWuct _offseto>nxxd->buf->page_loc}ages(xto ->aclWflagtV|= tva4_ACL_TRUNC;s	c(ded page NFS: acl reply:STR4_locV%uV> page_loc %ugs->2_)	a	TR4_loc, xgs->n, xdr layo);

)}essnt32(xdrncup(p++)-EOPNOTSUPPprrint:verflow:
 *up(pprdecode_op_nd_
dr(stru;
	efh*req,
		unsigned int *lund_hro_errnine(strucd, &operatOP_SAVEFH)NFdecode_op_nd_hdr(strusetTR4_m *xdr, unsigned int *lund_h2_t opnum;
h,	p = xdrbmlocatind_h *up(p;
dencup(p++)ine(strucd, &operatOP_SETRTEDsNFS_OK)0;
outflowto_errn *up(p;
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpbmloc);

	p = xdr_inline_ecode(xdr, 8);
	if (unlikelybmloc)layo)p))
		gxpectedpst2_)rflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struset,lienti *req, stunsigned int *lentreturn_argsset,lienti _ct c*res_nd_h2_t opnum;
h,	p = xdropnum;
	str= xdr_arerr;
decode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_copnumV_r	if (nfserr ==++);de
		gopnumV!_rOP_SETCLIENTIDfages(ned page _argsdr(struset,lienti : Seration"
		" %doINTat
	s"2_)	"d%dgs->lopnum)tva)rflow:
-E;
}

ste_arerr);

	p = xdr_inline_e
		g_arerr);+)tvalOKpages(code(xdr, 4);
	if (unlikely8)+ tva4_VERIFIER
ps(&hpr))
		goto out_overfloww;
	*len = be32_to_cpScode(xdr+ 1;
	enUINT64);&to ->)lienti );
s(memcpygto ->)
nfirm.uct coplytva4_VERIFIER
ps(&hpr)snt32( _OK)_arerr);+)tvaERR_CLID_INUS&hages(,	p = xdrlocat
s(/* skiptneti  g;
	rerges(Scode(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlocs_r	if (nfserr ==);s(Scode(xdr, 4);
	if (unlikely= be32_)
		goto out_overfloww;
	*len = be32_to_c
s(/* skiptuadgs g;
	rerges(Scode(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSlocs_r	if (nfserr ==);s(Scode(xdr, 4);
	if (unlikely= be32_)
		goto out_overfloww;
	*len = be32_to_c_)rflow:
-tvaERR_CLID_INUS&;essnt32(xdrrflow:
o(nfserr)(nfserrno)_arerr);
FSrflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(struset,lienti _confirmm *xdr, unsigned int *lund_hro_errnine(strucd, &operatOP_SETCLIENTID_CONFIRM)NFode_compound_hdr(strurqst *req,
		unsigned int *lennce_args *_{gio_ct c*res_nd_h2_t opnum;
hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_>bitm)NFS_OK)0;
out_at_to_errn *up(p;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cto ->ccv_bV_r	if (nfserr ==++);deto ->c__,->comminnerV_r	if (nfserr ==++);deto_errnine(strrqst uc__ifieamxgs, &to ->c__,->c__ifieauovint_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strudelegtatic m *xdr, unsigned int *lund_hro_errnine(strucd, &operatOP_DELEGRETURN)NFdecode_op_nd_hdr(struse, arg_gs *req,
		unsigned int *le
fs41_____treturn_argsse, arg4 **lavoi_nd_h,opnoi _ {catixdr_inline_
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpoi _ {c);

	p = xdr_inline_e
		goi _ {c)> GSalOID_MAX_LENflow;
	*len =err;
decode(xdr, 8);
	if (unlikelyoi _ {c!p))
		goto out_overflow;
	*len = be32_to_cpmemcpygflavoi->flavoi_ arg.oi .uct coplyoi _ {c!p))flavoi->flavoi_ arg.oi . {c);
oi _ {catFScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cflavoi->flavoi_ arg.qopV_r	if (nfserr == = be3flavoi->flavoi_ arg.argsicRV_r	if (nfserr == NFderflow:
	prvint_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decen =err:
static int NVALNFdecode_op_nd_hdr(struse, arg_comm	sm *xdr, unsigned int *lentreturn_argsse, arg_ct c*res_nd_htreturn_argsse, arg4 *se,_flavoi;
h, char **striily
um_flavoisatind_h *up(p;
hxdr_inline_
ecode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cdeto ->flavois->num_flavois);
0pr
num_flavois);
	if (nfserr == NFdeym, (iV_ret i <r
um_flavoisa i++)ages(ne,_flavoir= &to ->flavois->flavois[i]32_)
		ggDR_QUAD&ne,_flavoib1t)- gDR_QUADto ->flavois)> PAGE_ps(&hat_
aed k;es(Scode(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpSne,_flavoi->flavoi);
	if (nfserr == NFdeS_OK)0e,_flavoi->flavoi);= RPC_AUTH_GSa}ages(xncup(p++)ine(strce, arg_gs * *lente,_flavoi!p)))S_OK)0;
out_at_ow;
	*len pr
)}es(to ->flavois->num_flavois++;
	}
dencup(p++)0print:verflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(strusec arg*req,
		unsigned int *lentreturn_argsse, arg_ct c*res_nd_hstri *up(p++)ine(strucd, &operatOP_SECINFO)NFS_OK)0;
out_at_to_errn *up(p;
hro_errnine(strse, arg_comm	smxgs, to _;_dec#_OKdef);
d(CONFIG_tvalV4_1)code_op_nd_hdr(struse, arg_no_qst *req, stunsigned int *lentreturn_argsse, arg_ct c*res_nd_hstri *up(p++)ine(strucd, &operatOP_SECINFO_NO_Ntat)NFS_OK)0;
out_at_to_errn *up(p;
hro_errnine(strse, arg_comm	smxgs, to _;_decode_op_nd_hdr(strunp__bumreq, stunsigned int *lentreturn_argsnp__bu *np__buund_h2_t opnum;
h,	p = xdrbrgs->_, xdr;
h, char **striie_
ecode(xdr, 8);
	if (unlikely(!p))brgs->_, xdrV_r	if (nfserr ==++);de
		gbrgs->_, xdrV> tva4_OP_MAP
NUMATTRSStat_to_errnse;
}

code(xdr, 8);
	if (unlikely( * brgs->_, xdrhpr	ym, (iV_ret i <rbrgs->_, xdr; i++)es(np__bu->u., xdr[i]V_r	if (nfserr ==++);dterflow:
	prode_compound_hdr(struexcs, &h_ dm *xdr, unsigned int *le nfs1_____treturn_arg1uexcs, &h_ d_ct c*res_nd_h2_t opnum;
h,	p = xdrdummy;
hDR_QUAdummyigneatind_h *up(p;
h,	p = xdrimpl_i _cov_b32dencup(p++)ine(strucd, &operatOP_EXCHANGE_Ih)NFS_OK)0;
out_at_to_errn *up(p;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_c(xdr+ 1;
	enUINT64);&to ->)lienti );
scode(xdr, 8);
	if (unlikely12 NFS_OK)oto out_overflow;
	*len = be32_to_cpto ->gs-t V_r	if (nfserr ==++);deto ->flagtV_r	if (nfserr ==++);dcpto ->grr)e_protect.how);

	p = xdr_inline_enwitch	gto ->grr)e_protect.howfagescase SP4_NON&:r)
aed k;escase SP4_MACH_CREh:es(ncup(p++)ine(strucd_bum *len&to ->grr)e_protect.eargrc up)))
		gncup(p)
	t_to_errn *up(p;
h(ncup(p++)ine(strucd_bum *len&to ->grr)e_protect.allowup)))
		gncup(p)
	t_to_errn *up(p;
h(aed k;esdefault:ve	WARN_ON_ONCE(1)tva)rflow:
-E;
}

stde/* gerati_ownea4.so_minoi_ dVges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_ccode(xdr+ 1;
	enUINT64);&to ->gerati_ownea->minoi_ d}

de/* gerati_ownea4.so_majoi_ dVges(ncup(p++)ine(struct xdr, 8);
m *len&dummyen&dummyigne!p))
		goto out_o0;
out_flowto_errn *up(p;
e
		goto out_odummyV> tva4_OPAQUE_LIMIT!tat_to_errnse;
}

memcpygto ->gerati_ownea->majoi_ d, dummyigne, dummy)_cpto ->gsrati_ownea->majoi_ d_sz++)iummy;
de/* gerati_scope4Vges(ncup(p++)ine(struct xdr, 8);
m *len&dummyen&dummyigne!p))
		goto out_o0;
out_flowto_errn *up(p;
e
		goto out_odummyV> tva4_OPAQUE_LIMIT!tat_to_errnse;
}

memcpygto ->gerati_scope->gerati_scope, dummyigne, dummy)_cpto ->gsrati_scope->gerati_scope_sz++)iummy;
de/* Impleme_fat
	s IdVges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpimpl_i _cov_bV_r	if (nfserr ==++);dcp
		gimpl_i _cov_bfages(/* nii_domainrges(Sncup(p++)ine(struct xdr, 8);
m *len&dummyen&dummyigne!p))(
		goto out_o0;
out_flowwto_errn *up(p;
h(
		goto out_odummyV> tva4_OPAQUE_LIMIT!tat__to_errnse;
}

)memcpygto ->impl_i ->iomain, dummyigne, dummy)_ces(/* nii_qst rges(Sncup(p++)ine(struct xdr, 8);
m *len&dummyen&dummyigne!p))(
		goto out_o0;
out_flowwto_errn *up(p;
h(
		goto out_odummyV> tva4_OPAQUE_LIMIT!tat__to_errnse;
}

)memcpygto ->impl_i ->qst , dummyigne, dummy)_ces(/* nii_dat  ges(Scode(xdr, 4);
	if (unlikely12 NFS)
		goto out_overfloww;
	*len = be32_to_cpScode(xdr+ 1;
	enUINT64);&to ->impl_i ->iat .se,ondrhpr		to ->impl_i ->iat .nse,ondr);
	if (nfserr == NFdeS/* ifVthere's moren bin one e_fryenharoren be qsstrges(sterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strucs, WaR4_sm *xdr, unsigned int *le nfs1____nceturn_argscs, nelWaR4_s *aR4_s_nd_h2_t opnum;
h,opnn_WaR4_s, val;
FScode(xdr, 4);
	if (unlikely2(!p))
		goto out_overflow;
	opnum = be32_to_cvalV_r	if (nfserr ==++);S/* heederpadsz+ges(
		gvaltat_to_errnse;NVALNs(/* no suppornaym, heeder pt ding yetrges(aR4_s->maxWrqsd_sz++)	if (nfserr ==++);deaR4_s->maxWresp_sz++)	if (nfserr ==++);deaR4_s->maxWresp_sz_cacherV_r	if (nfserr ==++);deaR4_s->maxWoprV_r	if (nfserr ==++);deaR4_s->maxWreqrV_r	if (nfserr ==++);den_WaR4_s);

	p = xdr_inline_e
		goto out_on_WaR4_s)> 1)fages(c(__fgeKERN_WARNING  NFS: argsInvalid rdma cs, nel aR4_s)"2_)	"ccv_bV%ugs->lastbytewr n_WaR4_s)tva)rflow:
-E;NVALNF
ste_OK)
_WaR4_s);= 1)ages(code(xdr, 4);
	if (unlikely(!p /* skiptrdmaWaR4_s *es(S
		goto out_overfloww;
	*len = be32_to_cpsterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strusess
	si *req, stunsigned int *lentreturn_argssess
	si  *arg_nd_hro_errnine(struct xdrfixegm *lensi ->iat cotva4_MAX_SESSIONID_LENfecode_compound_hdr(strubin _conn= xdsess
	sm *xdr, unsigned int *le nfs4treturn_arg1ubin _conn= xdsess
	s_ct c*res_nd_h2_t opnum;
hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_BIND_CONN_TO_SESSION)NFS_OK)!0;
out_at_ncup(p++)ine(strcess
	si * *len&to ->gess
	s->gess_rguove
		goto out_o0;
out_flowto_errn *up(p;
cp/* dir flagt, rdma modeVboolVges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cpto ->iii);
	if (nfserr ==++);de
		gto ->iii);= 0 || to ->iii)> tva4_CDva4_BOTHtat_to_errnse;
}

_OK)a	p = xdr_inlin++_refes(to ->use_conn=is_cdmaWmodeV_rfalsR;
	t32(xdrrf ->use_conn=is_cdmaWmodeV_rrete;
FSrflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(strucreat dsess
	sm *xdr, unsigned int *le nfs4_treturn_arg1ucreat dsess
	s_ct c*res_nd_h2_t opnum;
hnd_h *up(p;
once_args *_)lient *clpV_rto ->)lient;_htreturn_argssess
	s *sess
	s = clp->)lssess
	s;
dencup(p++)ine(strucd, &operatOP_CREATE_SESSION)NFS_OK)!0;
out_at_ncup(p++)ine(strcess
	si * *len&gess
	s->gess_rguove
		goto out_o0;
out_flowto_errn *up(p;
cp/* gs-t , flagtVges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_cclp->)lsse-t V_r	if (nfserr ==++);degess
	s->flagtV_r	if (nfserr ==}

de/* Cs, nel aR4_c voisVges(ncup(p++)ine(strcs, WaR4_sm *len&gess
	s->fcWaR4_s)tva_OK)!0;
out_at_ncup(p++)ine(strcs, WaR4_sm *len&gess
	s->bcWaR4_s)tvarflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static bool __decode_op_nd_hdr(strudsstroydsess
	sm *xdr, unsigned int *le voi  *dummy)nd_hro_errnine(strucd, &operatOP_DESTROY_SESSION)NFdecode_op_nd_hdr(strudsstroyd,lienti *req, stunsigned int *lenvoi  *dummy)nd_hro_errnine(strucd, &operatOP_DESTROY_CLIENTIDfprdecode_op_nd_hdr(strureclaim_complet *req,
		unsigned int *lenvoi  *dummy)nd_hro_errnine(strucd, &operatOP_RECLAIM_COMPLEtm)NF}
#end_OK/* CONFIG_tvalV4_1rges
_compound_hdr(struse xdncom *xdr, unsigned int *le nfs___treturn_argsse xdnco_ct c*rese nfs___treturnrpc_rqsdr*rqsduund_#_OKdef);
d(CONFIG_tvalV4_1)chtreturn_argssess
	s *sess
	s;_htreturn_argssess
	si  i }

,opndummy;
hnd_h *up(p;
hxdr_inline_
e
		gto ->ssiglotc__rtULLf2_)rflow:
	pra_OK)!to ->ssiglot->table->gess
	sf2_)rflow:
	prdencup(p++)ine(strucd, &operatOP_SEQUENCE)NFS_OK)!0;
out_at_ncup(p++)ine(strcess
	si * *len&rguove
		goto out_o0;
out_flow;
	*len =err;
de/*
	 * If thesseration"
		"s diffeaent valut cym, cess
	sIDenslotID or
	 * se xdncor
umbeaenthesseratiois looney tunes.
	 */))ncup(p++)-EREMOTe;
}

sess
	s = to ->ssiglot->table->gess
	se_
e
		gmemcmp(i .uct cogess
	s->gess_rg.uct c
fs___tva4_MAX_SESSIONID_LENffages(ned page %ssInvalid sess
	s irgs-> astbytew NFow;
	*len =err;

stdecode(xdr, 4);
	if (unlikely20!p))
		goto out_overflow;
	opnum = be32_to_cp/* gs-t  */))dummyV;
	if (nfserr ==++);de
		gdummyV!= to ->ssiglot->gs-_nrfages(ned page %ssInvalid se xdncor
umbeags-> astbytew NFow;
	*len =err;

stp/* glotct  */))dummyV;
	if (nfserr ==++);de
		gdummyV!= to ->ssiglot->glot_nrfages(ned page %ssInvalid slotct gs-> astbytew NFow;
	*len =err;

stp/* highsstrglotct  */))to ->ssihighsstiglott V_r	if (nfserr ==++);de/* target highsstrglotct  */))to ->ssitargetihighsstiglott V_r	if (nfserr ==++);de/* to ult+flagtVges(to ->ssigcup(p_flagtV_r	if (nfserr ==}

encup(p++)0print=err:
sta ->ssigcup(p =n *up(p;
hro_errn *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

sncup(p++)-E;
}

;
	*len =err;
#t32( K/* CONFIG_tvalV4_1rges)rflow:
	pr#end_OK/* CONFIG_tvalV4_1rgesdec#_OKdef);
d(CONFIG_tvalV4_1)code_op_nd_hdr(strugetdesicR arg*req,
		unsigned int *le nfs4treturnparg*desicRlindev_nd_h2_t opnum;
h,	p = xdrloc, tSER;
	strn *up(p;
FSncup(p++)ine(strucd, &operatOP_GETDEVICEINFO)NFS_OK)0;
out_ages(_OK)0;
outV== ooTOOSMALLfag nfscode(xdr, 4);
	if (unlikely(!p)))S
		goto out_overflowww;
	*len = be32_to_cpS	ndev->minccv_bV_r	if (nfserr == NFS	ened page argsMinr)
v_b too.small.rm->c_bV_r%ugs->2_)	areturn -EIndev->minccv_b);

)}eswto_errn *up(p;
hstdecode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	opnum = be32_to_c_SERV_r	if (nfserr ==++);de
		gu_toV!= ndev->layintWq_tofages(ned page %s: gayintSmismatch	req:V%uVndev:r%ugs->2_)	return -EIndev->layintWq_to, tSER)tva)rflow:
-E;NVALNF
ste/*
	 * Get*therds.\n"VofVthesoct xd desicR_adgs4.e(xdrqst _pages places
	 * thesoct xd desicR_adgs4*/
	thesxnsibuf->pages (parg*desicR->pages)
	 * cod placesn be qsmaining xns uct oinsxnsibuf->tail
	 */))ndev->minccv_bV_r	if (nfserr == NFS
		g(xdrqst _pageslikelyndev->minccv_b)V!= ndev->minccv_b)low;
	opnum = be32_to_cp/* Par2( nottfi)
{
	snxdr_bp, c__ifying  bitmitois zero.Vges(]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cploc);

	p = xdr_inline_e
		gxochages(,	p = xdri;es(Scode(xdr, 4);
	if (unlikely( * = be32_)
		goto out_overfloww;
	*len = be32_to_c
s(_OK)a	p = xdr_inli++)a&
fs___ ~(NOTIFY_DEVICEID4_CHANGE | NOTIFY_DEVICEID4_DELEtm)fag nfsned page %s: unsupporner nottfi)
{
	sgs->2_)	aastbytew NF	hstde	ym, (iV_r1t i <r {ca i++)ages((_OK)a	p = xdr_inli++))ages((sned page %s: unsupporner nottfi)
{
	sgs->2_)	aaastbytew NF	h__to_errnse;
}

))}esw}cpsterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strulayintgetm *xdr, unsigned int *lennce_argrpc_rqsdr*req,2_)	l___treturn_argslayintget_ct c*res_nd_h2_t opnum;
hnd_h *up(p;
o,] = ayintW)
v_b32_,] =tocvdatdencup(p++)ine(strucd, &operatOP_LAYOUTGET)NFS_OK)0;
out_at_to_errn *up(p;
hcode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpto ->to_err_onWclos );

	p = xdr_inline_eine(strc_rqst *xgs, &to ->err)erguove]ode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cplayintW)
v_b);

	p = xdr_inline_e
		g!layintW)
v_bfages(ned page %s: seration"sponded win"Vempty gayintSarraygs->2_)	return -)tva)rflow:
-E;NVALNF
stFScode(xdr, 4);
	if (unlikely2(!p))
		goto out_overflow;
	opnum = be32_to_ccode(xdr+ 1;
	enUINT64);&to ->r, &h.offset);_ccode(xdr+ 1;
	enUINT64);&to ->r, &h.ds.\n")NFSto ->r, &h.iomodeV_r	if (nfserr ==++);deto ->_SERV_r	if (nfserr ==++);deto ->rayintp->dsc);

	p = xdr_inline_vened page ar roff:%lu _loc:%lu _iomode:%d, loWq_to:0x%x, lo.loc:%rgs->2_)return -E2_)(, char **leng)to ->r, &h.offsetE2_)(, char **leng)to ->r, &h.ds.\n"axdrrf ->r, &h.iomodeaxdrrf ->q_to,xdrrf ->rayintp->dsc);dcptocvdode(xdrqst _pagesm *lenrf ->rayintp->dsc);de
		gto ->rayintp->dsc)> tocvdpages(ded page NFS: args)
tchee_ong*/
	layintgetVreply:S"2_)	a"gayintSlocV%uV> tocvd %ugs->F	h__to ->rayintp->dsc, tocvd)tva)rflow:
-E;NVALNF
stFS
		gxayintW)
v_b)> 1)ages(/* We only ncode_rards.\n"VoneSarray at*thermome_f.  Any
_)l* further e_frit ccre just harored.  Note  bitmthis meanr2_)l* ther)lient may se_rarn"sponse  bitmis lessn bin  be
	)l* minimummitore xdsner.
		 */))(ned page %s: seration"sponded win"V%d	layints, dropping  ailgs->2_)	return -EIlayintW)
v_bfNF
stFSrflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strulayinttatic m *xdr, unsigned int *le nfs1______treturn_argslayintto_err_ct c*res_nd_h2_t opnum;
hnd_h *up(p;
dencup(p++)ine(strucd, &operatOP_LAYOUTRETURN)NFS_OK)0;
out_at_to_errn *up(p;
hcode(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpto ->lrs_pto e_b);

	p = xdr_inline_e
		gto ->lrs_pto e_b_at_ncup(p++)ine(strc_rqst *xgs, &to ->err)erguoverflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strulayintcomminm *xdr, unsigned int *le nfs1______treturnrpc_rqsdr*req,2_)	l______treturn_argslayintcommin_ct c*res_nd_h2_t opnum;
h__,] =hat cs, &hdatind_h *up(p;
dencup(p++)ine(strucd, &operatOP_LAYOUTCOMMIT!_cpto ->gcup(p =n *up(p;
h_OK)0;
out_at_to_errn *up(p;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cphat cs, &hd);

	p = xdr_inline_ve_OK)0at cs, &hd)ages(/* throw away new sat ages(Scode(xdr, 4);
	if (unlikely8 NFS)
		goto out_overfloww;
	*len = be32_to_cpsterflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

static int decode_compound_hdr(strutsstig_rqst * *xdr, unsigned int *le nfs1______treturn_arg1utsstig_rqst _ct c*res_nd_h2_t opnum;
hnd_h *up(p;
oitrinum_ct ;
dencup(p++)ine(strucd, &operatOP_TEST_STATEIh)NFS_OK)0;
out_at_to_errn *up(p;
FScode(xdr, 4);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpcum_ct c_r	if (nfserr ==++);de
		gcum_ct c!= 1)xdr;
	*len pr))code(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cpto ->ncup(p++)	if (nfserr ==++);dterflow:
 *up(pprint_overflow_msg(__func__, xdr);
	return -EIO;
}

int:verflow:
nt decode_compound_hdr(strufreeig_rqst * *xdr, unsigned int *le nfs1______treturn_arg1ufreeig_rqst _ct c*res_nd_hto ->ncup(p++)ine(strucd, &operatOP_FREE_STATEIh)NFSrflow:
to ->ncup(pNF}
#end_OK/* CONFIG_tvalV4_1rges
/*
 * END OF "GENERIC" DECODE ROUTINES.
rgesc/*
 * Dne(st OPEN_DOWNGRADErn"sponse
rgesode_op_nd_h_args(xdr+ 1unpenrdowngraunlreq, strpc_rqsdr*rqsdue nfs41_____ req,
		unsigned int *le nfs4lllllllnce_args *_clos ct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strnpenrdowngraunl *lenrf )NFS_OK)0;
outa!_reflow;
	*len pr
dr(strugetfTR4_m *lenrf ->faR4_, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st ACCESSrn"sponse
rgesode_op_nd_h_args(xdr+ 1uacces *req,
		rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl______treturn_argsacces ct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
outa!_reflow;
	*len pr
ncup(p++)ine(stracces * *len&to ->gupporneren&to ->acces )NFS_OK)0;
outa!_reflow;
	*len pr
dr(strugetfTR4_m *lenrf ->faR4_, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st LOOKUPrn"sponse
rgesode_op_nd_h_args(xdr+ 1urpokuum *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl______treturn_argsrpokuu_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrpokuumxdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfh* *lenrf ->fh)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfTR4_Wlabelm *lenrf ->faR4_, to ->label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st LOOKUP_ROOTrn"sponse
rgesode_op_nd_h_args(xdr+ 1urpokuu_rootlreq, strpc_rqsdr*rqsdue nfs41___req,
		unsigned int *le nfs4lllltreturn_argsrpokuu_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputrootfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfh* *lenrf ->fh)NFS_OK)0;
out++_refes(ncup(p++)ine(strgetfTR4_Wlabelm *lenrf ->faR4_,2_)	aa	to ->label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st REMOVtrn"sponse
rgesode_op_nd_h_args(xdr+ 1uremnc_*req, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl______treturn_aruremnc_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strremnc_* *len&to ->, arguprint:verflow:
 *up(pprdec/*
 * Dne(st RENtatrn"sponse
rgesode_op_nd_h_args(xdr+ 1ureqst *req, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl______treturn_arureqst ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strs
	efh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strreqst * *len&to ->old_, arg,n&to ->new_, arguprint:verflow:
 *up(pprdec/*
 * Dne(st LINKrn"sponse
rgesode_op_nd_h_args(xdr+ 1urst *req, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl____treturn_argsrst _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strs
	efh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrst * *len&to ->, arguprS_OK)0;
out_at_;
	*len pr
/*
	 * Note order:tOP_   F le
	esn be directory asn be curaent
	 *             filencode_.
	 */))ncup(p++)ine(strrestorefh*xdr)NFS_OK)0;
out_at_;
	*len pr
ine(strgetfTR4_Wlabelm *lenrf ->faR4_, to ->label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st CREATErn"sponse
rgesode_op_nd_h_args(xdr+ 1ucreat m *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl______treturn_argscreat dct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strcreat m *len&to ->di__, arguprS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfh* *lenrf ->fh)NFS_OK)0;
out_at_;
	*len pr
ine(strgetfTR4_Wlabelm *lenrf ->faR4_, to ->label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st SYMLINKrn"sponse
rgesode_op_nd_h_args(xdr+ 1u _rqst m *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfs	treturn_argscreat dct c*res_nd_hrflow:
o(nfs(xdr+ 1ucreat mrqsdue  *lenrf )NF}
c/*
 * Dne(st GETRTEDrn"sponse
rgesode_op_nd_h_args(xdr+ 1ugetTR4_m *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfs	treturn_argsgetTR4_dct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfTR4_Wlabelm *lenrf ->faR4_, to ->label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Ene(st an SETRCLore xdsn
rgesode_op_voi  _args(xdrdncusetTclm *xdr, rpc_rqsdr*req,ntreturnunsigned int *le nfs	treturn_arusetTclargtVgargt_nd_htreturncomp
v_dd, & , &++)ges(.minoiatis
	s = _args(xdrminoiatis
	s(&argt->geq_argt_e n};
deene(strcomp
v_dd, &*xgs, req,n&hdr)NFSene(strse xdncom *len&argt->geq_argt,n&hdr)NFSene(strputfh*xdr, argt->fh,n&hdr)NFSene(strsetTclmxdr, argt,n&hdr)NFSene(strnops(&hdr)NF}
c/*
 * Dne(st SETRCLoresponse
rgesode_op_nd_
_args(xdr+ 1u etTclm *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nf____treturn_arusetaclct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strsetTR4_mO;
}

int:verflow:
ncup(pNF}
c/*
 * Dne(st GETRCLoresponse
rgesode_op_nd_
_args(xdr+ 1ugetTclm *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nf____treturn_arugetaclct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
de
		gto ->aclWscratch	!detULLf)ges(voi  *codepage_adgses *to ->aclWscratch NFS)unsigetWscratch_buffealikelyn, PAGE_ps(&h;r
stencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetTclmxdr, rqsdue rf )NF
int:verflow:
ncup(pNF}
c/*
 * Dne(st CLOSErn"sponse
rgesode_op_nd_h_args(xdr+ 1uclos m *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl_____nce_args *_clos ct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strclos m *lenrf )NFS_OK)0;
outa!_reflow;
	*len pr
/*
	 * Note: Seratiomay do delete on clos )ym, this file
	 * ind which	case  be getTR4_ call)will)fail win"
	 * ian ESTALE erroa. Shouldn't)berasproblem,
	 * 	thoughensincorfaR4_->valid will)qsmain unset.
	 */))dr(strugetfTR4_m *lenrf ->faR4_, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st OPENrn"sponse
rgesode_op_nd_h_args(xdr+ 1unpen*req, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl____treturn_arunpenct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strnpen* *lenrf )NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfh* *len&rf ->fh)NFS_OK)0;
out_at_;
	*len pr

		gto ->acces _re xdsn_at_ine(stracces * *len&to ->acces _gupporneren&to ->acces _ct ultne_eine(strgetfTR4_Wlabelm *lenrf ->f_aR4_, to ->f_label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st OPEN_CONFIRMrn"sponse
rgesode_op_nd_h_args(xdr+ 1unpenrconfirmm *xdr, rpc_rqsdr*rqsdue nfs41____req,
		unsigned int *le nfs4llllltreturn_arunpen_confirmct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strnpenrconfirmm *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st OPENrn"sponse
rgesode_op_nd_h_args(xdr+ 1unpenrnoTR4_m *xdr, rpc_rqsdr*rqsdue nfs41___req,
		unsigned int *le nfs4lllltreturn_arunpenct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strnpen* *lenrf )NFS_OK)0;
out_at_;
	*len pr

		gto ->acces _re xdsn_at_ine(stracces * *len&to ->acces _gupporneren&to ->acces _ct ultne_eine(strgetfTR4_m *lenrf ->f_aR4_, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st SETRTEDrn"sponse
rgesode_op_nd_h_args(xdr+ 1usetTR4_m *xdr, rpc_rqsdr*rqsdue nfs	treturnunsigned int *le nfs	treturn_arusetTR4_ct c*res_nd_hnce_argcomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strsetTR4_mO;
}

S_OK)0;
out_at_;
	*len pr
ine(strgetfTR4_Wlabelm *lenrf ->faR4_, to ->label, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st LOCKrn"sponse
rgesode_op_nd_h_args(xdr+ 1uroc *req, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl____treturn_aruroc dct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrpckm *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st LOCKTrn"sponse
rgesode_op_nd_h_args(xdr+ 1urpcktlreq, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl_____nce_args *_rpcktdct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrpcktm *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st LOCKUrn"sponse
rgesode_op_nd_h_args(xdr+ 1urpckulreq, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl_____nce_args *_rpckudct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrpckum *lenrf )NFint:verflow:
ncup(pNF}
code_op_nd_h_args(xdr+ 1ureleaserrpckowneam *xdr, rpc_rqsdr*rqsdue nfs		__req,
		unsigned int *lenvoi  *dummy)nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strreleaserrpckowneamx;
}

static incup(pNF}
c/*
 * Dne(st s(&h   Frn"sponse
rgesode_op_nd_h_args(xdr+ 1uret qst *req, strpc_rqsdr*rqsdue nfs41req,
		unsigned int *le nfs4ltreturn_argsret qst dct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strret qst *xdr, rqsduuprint:verflow:
 *up(pprdec/*
 * Dne(st RE&hDIRrn"sponse
rgesode_op_nd_h_args(xdr+ 1uret di_m *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfs	treturn_argsret di_dct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strret di_mxdr, rqsdue rf )NFint:verflow:
 *up(pprdec/*
 * Dne(st Ret rn"sponse
rgesode_op_nd_h_args(xdr+ 1uret *req, strpc_rqsdr*rqsdue req,
		unsigned int *le nfsl____treturn_aru{gio_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strreadmxdr, rqsdue rf )NFS_OK)!0;
out_at_ncup(p++)to ->,
v_b32int:verflow:
 *up(pprdec/*
 * Dne(st >bitmrn"sponse
rgesode_op_nd_h_args(xdr+ 1urqst *req,
		rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl_____nce_args *_{gio_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrqst * *lenrf )NFS_OK)0;
out_at_;
	*len pr

		gto ->faR4__at_ine(strgetfTR4_m *lenrf ->faR4_, to ->gsrati}

S_OK)!0;
out_at_ncup(p++)to ->,
v_b32int:verflow:
 *up(pprdec/*
 * Dne(st COMMITrn"sponse
rgesode_op_nd_h_args(xdr+ 1ucomminm *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl______treturn_arucomminct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strcomminm *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st FSINFOrn"sponse
rgesode_op_nd_h_args(xdr+ 1ufs arg*req,
		rpc_rqsdr*req,ntreturnunsigned int *le nfsl______treturn_argsfs arg_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strce xdncom *len&to ->geq_ct , teq)NFS_OK)!0;
out_at_ncup(p++)ine(strputfh*xdr)NFS_OK)!0;
out_at_ncup(p++)ine(strfs arg* *lenrf ->fs arguprSrflow:
ncup(pNF}
c/*
 * Dne(st PATHCONFrn"sponse
rgesode_op_nd_h_args(xdr+ 1upathconfm *xdr, rpc_rqsdr*req,ntreturnunsigned int *le nfs	_treturn_argspathconf_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strce xdncom *len&to ->geq_ct , teq)NFS_OK)!0;
out_at_ncup(p++)ine(strputfh*xdr)NFS_OK)!0;
out_at_ncup(p++)ine(strpathconfm *lenrf ->pathconfuprSrflow:
ncup(pNF}
c/*
 * Dne(st STATFSrn"sponse
rgesode_op_nd_h_args(xdr+ 1uode_f *req,
		rpc_rqsdr*req,ntreturnunsigned int *le nfsl______treturn_argsode_f _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strce xdncom *len&to ->geq_ct , teq)NFS_OK)!0;
out_at_ncup(p++)ine(strputfh*xdr)NFS_OK)!0;
out_at_ncup(p++)ine(strode_f * *lenrf ->fsode_uprSrflow:
ncup(pNF}
c/*
 * Dne(st GETRTED_BITMAPrn"sponse
rgesode_op_nd_h_args(xdr+ 1ugerati_cap *req,
		rpc_rqsdr*req, nfs41___req,
		unsigned int *le nfs4lllltreturn_argsgerati_cap _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , teq)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgerati_cap * *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st RENEWrn"sponse
rgesode_op_nd_h_args(xdr+ 1ureqewm *xdr, rpc_rqsdr*rqsdue req,
		unsigned int *le nfsl_____voi  *__unused)nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strreqewmx;
}

static incup(pNF}
c/*
 * Dne(st SETCLIENTIDrn"sponse
rgesode_op_nd_h_args(xdr+ 1uset,lienti *req, strpc_rqsdr*req, nfs41___req,
		unsigned int *le nfs4lllltreturn_argsget,lienti _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strcet,lienti * *lenrf )NFStatic incup(pNF}
c/*
 * Dne(st SETCLIENTID_CONFIRMrn"sponse
rgesode_op_nd_h_args(xdr+ 1uset,lienti _confirmm *xdr, rpc_rqsdr*req, nfs441___req,
		unsigned int *l_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strcet,lienti rconfirmm *l)NFStatic incup(pNF}
c/*
 * Dne(st DELEGRETURNrn"sponse
rgesode_op_nd_h_args(xdr+ 1udelegtatic m *xdr, rpc_rqsdr*rqsdue nfs41___req,
		unsigned int *le nfs4lllltreturn_argsdelegtatic ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
outa!_reflow;
	*len pr
ncup(p++)ine(strgetfTR4_m *lenrf ->faR4_, to ->gsrati}

S_OK)0;
outa!_reflow;
	*len pr
ncup(p++)ine(strdelegtatic mx;
}

int:verflow:
ncup(pNF}
c/*
 * Dne(st FS_LOCATIONSrn"sponse
rgesode_op_nd_h_args(xdr+ 1u *_rpc
{
	s *req,
		rpc_rqsdr*req, nfs41____req,
		unsigned int *le nfs4llllltreturn_ar4u *_rpc
{
	s _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , teq)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr

		gto ->migr
{
	sf)ges(xxdrdntti_pagem *lenPAGE_ps(&h;r

ncup(p++)ine(strgetfTR4_Wgeqeric( *le nfs4	&rf ->f*_rpc
{
	s ->faR4_,2_)	aaetULLenrf ->fs_rpc
{
	s ,2_)	aaetULLenrf ->fs_rpc
{
	s ->gsrati}

SS_OK)0;
out_at__;
	*len pr


		gto ->reqew_at__ncup(p++)ine(strreqewmx;
}

s} t32( {r

ncup(p++)ine(strrpokuumxdr)NFSS_OK)0;
out_at__;
	*len pr

xxdrdntti_pagem *lenPAGE_ps(&h;r

ncup(p++)ine(strgetfTR4_Wgeqeric( *le nfs4	&rf ->f*_rpc
{
	s ->faR4_,2_)	aaetULLenrf ->fs_rpc
{
	s ,2_)	aaetULLenrf ->fs_rpc
{
	s ->gsrati}

S}
int:verflow:
ncup(pNF}
c/*
 * Dne(st SECINFOrn"sponse
rgesode_op_nd_h_args(xdr+ 1usec arg*req,
		rpc_rqsdr*rqsdue nfs	treturnunsigned int *le nfs	treturn_argsse, arg_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strsec arg* *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st FSID_PRESENTrn"sponse
rgesode_op_nd_h_args(xdr+ 1ufs d_pto e_bm *xdr, rpc_rqsdr*rqsdue nfs41____req,
		unsigned int *le nfs4llllltreturn_ar4ufs d_pto e_b_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strgetfh* *lenrf ->fh)NFS_OK)0;
out_at_;
	*len pr

		gto ->reqew_at_ncup(p++)ine(strreqewmx;
}

int:verflow:
ncup(pNF}
c#_OKdef);
d(CONFIG_tvalV4_1)c/*
 * Dne(st BIND_CONN_TO_SESSIONrn"sponse
rgesode_op_nd_h_args(xdr+ 1ubin _conn= xdsess
	sm *xdr, rpc_rqsdr*rqsdue nfs		req,
		unsigned int *le nfs4(voi  *res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strbin _conn= xdsess
	sm *lenrf )NFStatic incup(pNF}
c/*
 * Dne(st EXCHANGE_Ihrn"sponse
rgesode_op_nd_h_args(xdr+ 1uexcs, &h_ dm *xdr, rpc_rqsdr*rqsdue nfs41___req,
		unsigned int *le nfs4llllvoi  *res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strexcs, &h_ dm *lenrf )NFStatic incup(pNF}
c/*
 * Dne(st CREATE_SESSIONrn"sponse
rgesode_op_nd_h_args(xdr+ 1ucreat dsess
	sm *xdr, rpc_rqsdr*rqsdue nfs	l______treturnunsigned int *le nfs4lllll__treturn_arg1ucreat dsess
	s_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strcreat dsess
	sm *lenrf )NFStatic incup(pNF}
c/*
 * Dne(st DESTROY_SESSIONrn"sponse
rgesode_op_nd_h_args(xdr+ 1udestroydsess
	sm *xdr, rpc_rqsdr*rqsdue nfs		req,
		unsigned int *le nfs4(voi  *res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strdestroydsess
	sm *lenrf )NFStatic incup(pNF}
c/*
 * Dne(st DESTROY_CLIENTIDrn"sponse
rgesode_op_nd_h_args(xdr+ 1udsstroyd,lienti *req, strpc_rqsdr*rqsdue nfs		req,
		unsigned int *le nfs4(voi  *res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strdestroyd,lienti * *lenrf )NFStatic incup(pNF}
c/*
 * Dne(st SEQUENCErn"sponse
rgesode_op_nd_h_args(xdr+ 1use xdncom *xdr, rpc_rqsdr*rqsdue nfs41req,
		unsigned int *le nfs4ltreturn_argsse xdnco_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strce xdncom *lenct , tqsduuNFSrflow:
ncup(pNF}
c/*
 * Dne(st GET_LEASE_TIatrn"sponse
rgesode_op_nd_h_args(xdr+ 1uget_leasertit *req, strpc_rqsdr*rqsdue nfs	l______treturnunsigned int *le nfs4lllll__treturn_arguget_leasertit _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strce xdncom *len&to ->lsigeq_ct , tqsduuNFS_OK)!0;
out_at_ncup(p++)ine(strputrootfh*xdr)NFS_OK)!0;
out_at_ncup(p++)ine(strfs arg* *lenrf ->lsifs arguprSrflow:
ncup(pNF}
c/*
 * Dne(st RECLAIM_COMPLEtmrn"sponse
rgesode_op_nd_h_args(xdr+ 1ureclaim_complet *req,
		rpc_rqsdr*rqsdue nfs		_req,
		unsigned int *le nfs4(_treturn_arg1ureclaim_complet _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)!0;
out_at_ncup(p++)ine(strce xdncom *len&to ->geq_ct , tqsduuNFS_OK)!0;
out_at_ncup(p++)ine(strreclaim_complet * *lentULLfNFSrflow:
ncup(pNF}
c/*
 * Dne(st GETDEVINFOrn"sponse
rgesode_op_nd_h_args(xdr+ 1ugetdesicR arg*req,
		rpc_rqsdr*rqsdue nfs	l_____treturnunsigned int *le nfs4lllll_treturn_argugetdesicR arg_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
outa!_reflow;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
outa!_reflow;
	*len pr
ncup(p++)ine(strgetdesicR arg* *lenrf ->pdev_NFint:verflow:
ncup(pNF}
c/*
 * Dne(st LAYOUTGETrn"sponse
rgesode_op_nd_h_args(xdr+ 1urayintgetm *xdr, rpc_rqsdr*rqsdue nfs	l_treturnunsigned int *le nfs4lltreturn_argulayintget_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrayintgetmxdr, rqsdue rf )NFint:verflow:
 *up(pprdec/*
 * Dne(st LAYOUTRETURNrn"sponse
rgesode_op_nd_h_args(xdr+ 1urayinttatic m *xdr, rpc_rqsdr*rqsdue nfs41____req,
		unsigned int *le nfs4llllltreturn_ar4ulayintto_err_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrayinttatic m *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st LAYOUTCOMMITrn"sponse
rgesode_op_nd_h_args(xdr+ 1urayintcomminm *xdr, rpc_rqsdr*rqsdue nfs41____req,
		unsigned int *le nfs4llllltreturn_ar4ulayintcommin_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strrayintcomminmxdr, rqsdue rf )NFS_OK)0;
out_at_;
	*len pr
ine(strgetfTR4_m *lenrf ->faR4_, to ->gsrati}

int:verflow:
ncup(pNF}
c/*
 * Dne(st SECINFO_NO_Ntatrn"sponse
rgesode_op_nd_h_args(xdr+ 1use, arg_ng_nst *req, strpc_rqsdr*rqsdue nfs		req,
		unsigned int *le nfs4(treturn_argsse, arg_ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strputrootfh*xdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse, arg_ng_nst * *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st TEST_STATEIhrn"sponse
rgesode_op_nd_h_args(xdr+ 1utsstig_rqst * *xdr, rpc_rqsdr*rqsdue nfs41____req,
		unsigned int *le nfs4llllltreturn_ar41utsstig_rqst _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strtsstig_rqst * *lenrf )NFint:verflow:
ncup(pNF}
c/*
 * Dne(st FREE_STATEIhrn"sponse
rgesode_op_nd_h_args(xdr+ 1ufreeig_rqst * *xdr, rpc_rqsdr*rqsdue nfs41____req,
		unsigned int *le nfs4llllltreturn_ar41ufreeig_rqst _ct c*res_nd_htreturncomp
v_dd, & , &atind_h *up(p;
dencup(p++)ine(strcomp
v_dd, &*xgs, &hdr)NFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strse xdncom *len&to ->geq_ct , tqsduuNFS_OK)0;
out_at_;
	*len pr
ncup(p++)ine(strfreeig_rqst * *lenrf )NFint:verflow:
ncup(pNF}
#end_OK/* CONFIG_tvalV4_1rges
/**
 * _argsdee(strdiaent - Dne(st ansingle_tvav4 directory e_fry stored_nd
 *                      therdocalepage cache.
rg @ *l: XDR gned inwhere e_fry rf ides
rg @e_fry: buffea 	*lfill)nd win"Ve_fry uct 
rg @plus: boolean)nddi)
{
ngnwhether this should)berasret di_plusVe_fry
rg
rg R"
		"s zero _OKsucces ful, otherwiserasneg
{
ve errno valut is
rg rflow:er.
rg
rg This func{
	snis notctnvoked_dur
ngnRE&hDIRrn"ply)ine(s
ng, but
rg rather wheneatioan)appli)
{
	sntnvokes  be getde_fs(2) system call
rg 	sna directory alret y)nd our cache.
rg/
nd_h_argsdee(strdiaent(req,
		unsigned int *le_treturn_arue_fry *e_frye nf_______nd_hplus_nd_h, char **nd_h avem;
h,	p = xdrxdr_bp[3]++){0};
h,	p = xdrloc;_h2_t opnum de(xdr, 8);
	if (unlikely(!p))
		goto out_overflow;
	*len = be32_to_cp
		gum dde(xdrzerof)ges(code(xdr, 4);
	if (unlikely(!p)))
		goto out_overfloww;
	*len = be32_to_cpp
		gum dde(xdrzeroflowwrflow:
ntAGAIN_cppe_fry->eof _r1t
wwrflow:
ntBADCOOKIE;F
stFScode(xdr, 4);
	if (unlikely12!p))
		goto out_overflow;
	*len = be32_to_cpe_fry->prevrcookiRV_re_fry->cookiR;_ccode(xdr+ 1;
	enUINT64);&e_fry->cookiR)_cpe_fry->dsc);

	p = xdr_inline_vecode(xdr, 4);
	if (unlikelye_fry->dsc!p))
		goto out_overflow;
	*len = be32_to_cpe_fry->nst ode(consrnchar *) po_cp/*
	 * In	case  be seratiodoesn't)rflow:
an)nd(st number,
	 * worfakeVoneShere.  (Weodon't)use nd(st number 0,
	 * sincorglibc se_ms 	*lchokeVon it...)
	 */))e_fry->nd( _r1t
we_fry->faR4_->valid +)0pr))
		g+ 1;
	eTR4_Wxdr_bplikelyxdr_bp) <reflow;
	*len = be32_to_c
s
		g+ 1;
	eTR4_Wds.\n"m *len&dsc, & avem) <reflow;
	*len = be32_to_c
s
		g+ 1;
	egetfTR4_WTR4_slikelyxdr_bplye_fry->faR4_, e_fry->f"axdr	tULLene_fry->dabel, e_fry->gsrati} <reflow;
	*len = be32_to_cs
		ge_fry->faR4_->valid &_tva_RTED_FRTED_MOUNTED_ON_FILEIh)cppe_fry->nd( _re_fry->faR4_->m
v_bed_onWfileidatit32( 
		ge_fry->faR4_->valid &_tva_RTED_FRTED_FILEIh)cppe_fry->nd( _re_fry->faR4_->fileidat
we_fry->d__SERV_rDT_UNKNOWN_cs
		ge_fry->faR4_->valid &_tva_RTED_FRTED_TYPE)cppe_fry->d__SERV_r_aruum(strto_d_SER(e_fry->faR4_->m
de);dterflow:
0NF
int_overflow_msg(__func__, xdr);
	return -EIO;
}

static intAGAIN_c}
c/*
 * Weone **	*ltranslrqs

	twee:
o(n
ncup(p)rflow:
valuts cod
l* therdocaleerrno valuts which	may notcbe  be sst .
rgesode_op_treturn{tind_h *upatind_herrno_c}n_aruerrtbl[]++){
	{_tva4_OK,		0		},
	{_tva4ERR_PERM,		-EPERM		},
	{_tva4ERR_NOENT,	-ENOENT		},
	{_tva4ERR_IO,		-errno_tvaERR_IO},
	{_tva4ERR_NXIO,		-ENXIO		},
	{_tva4ERR_ACCESS,	-EACCES		},
	{_tva4ERR_EXIST,	-EEXIST		},
	{_tva4ERR_XDEV,		-EXDEV		},
	{_tva4ERR_NOTDIR,	-ENOTDIR	},
	{_tva4ERR_ISDIR,	-EISDIR		},
	{_tva4ERR_INVAL,	-EINVAL		},
	{_tva4ERR_FBIG,		-EFBIG		},
	{_tva4ERR_NOSPC,	-ENOSPC		},
	{_tva4ERR_ROFS,		-EROFS		},
	{_tva4ERR_MLINK,	-EMLINK		},
	{_tva4ERR_NAMETOOLONG,	-ENAMETOOLONG	},
	{_tva4ERR_NOTEMPTY,	-ENOTEMPTY	},
	{_tva4ERR_DQUOT,	-EDQUOT		},
	{_tva4ERR_STALE,	-ESTALE		},
	{_tva4ERR_BADHANDLE,	-EBADHANDLE	},
	{_tva4ERR_BAD_COOKIE,	-EBADCOOKIE	},
	{_tva4ERR_NOTSUPP,	-ENOTSUPP	},
	{_tva4ERR_TOOSMALL,	-ETOOSMALL	},
	{_tva4ERR_SERVERFAULT,	-EREMOTEIO	},
	{_tva4ERR_BADTYPE,	-EBADTYPE	},
	{_tva4ERR_LOCKED,	-EAGAIN		},
	{_tva4ERR_SYMLINK,	-ELOOP		},
	{_tva4ERR_OP_ILLEGAL,	-EOPNOTSUPP	},
	{_tva4ERR_D(&h OCK,	-ED(&h K	},
	{_-1,			-EIO		}
};
d/*
 * Conc__t
an)tva erroa e(st 	*lardocaleone.
rg This oneSis used jond_ly)by_tvav2 cod_tvav3.
rgesode_op_nd_
_argsode_rto_errno(nd_h *up_nd_hnd_hiatiym, (i +)0pn_aruerrtbl[i]. *upa!_r-1; i++f)ges(
		g_aruerrtbl[i]. *upa==h *up_nowwrflow:
_aruerrtbl[i].errno_c
ste_OK)0;
o <_r10000 ||h *up)> 10100)ages(/* Tbe seratiois looney tunes.ages(Static intREMOTEIO_c
ste/* If worcannotctranslrqs
 be erroa,
 be recnc__y rint);
s should
	 * ncode_rit.
	 * Note: qsmain
ngntvav4 erroa e(sts ncve valuts >r10000e_to should
	 * notcconfli)t win"Vn
{
ve Linux erroa e(sts.
	 */))tatic in *upat}
c#_Odef CONFIG_tvalV4_2c#_nclust "_arg2O;
.c"
#end_OK/* CONFIG_tvalV4_2rges
#def);
 PROC(proc, argq_to, rf q_to)nfs4\
[tvaPROC4_CLNT_##proc]++){	nfs4\
	.p_proc___= tvaPROC4_COMPOUND,nfs4\
	.p_ene(stode(kO;
eproc_t)_args(xdr##argq_to,s4\
	.p_+ 1;
	ode(kO;
dproc_t)_args(xdr##rf q_to,s4\
	.p_argdsc);
tva4_##argq_to##_sz,fs4\
	.p_n"plsc);
tva4_##rf q_to##_sz,fs4\
	.p_ode_odx_= tvaPROC4_CLNT_##proc,fs4\
	.p_nst o__= #proc,fs4s4\
}s
#def);
 STUB(proc)s4\
[tvaPROC4_CLNT_##proc]++){	\
	.p_nst o= #proc,f\F}
codxdr, rpc_proc arg	_argsprocedures[]++){
	PROC(RE&h,ppe_1uret ,t_ineuret _e nPROC(>bitm,ppe_1urqst ,t_ineurqst _e nPROC(COMMIT,ppe_1ucommin,t_ineucommin_e nPROC(OPEN,ppe_1unpen,t_ineunpen_e nPROC(OPEN_CONFIRM,pe_1unpenrconfirm,_ineunpenrconfirm_e nPROC(OPEN_NORTED,pe_1unpenrnoTR4_,_ineunpenrnoTR4__e nPROC(OPEN_DOWNGRADE,pe_1unpenrdowngraun,_ineunpenrdowngraun_e nPROC(CLOSE,ppe_1uclos ,t_ineuclos _e nPROC(SETRTED,ppe_1usetTR4_,t_ineusetTR4__e nPROC(FSINFO,ppe_1ufs arg,t_ineufs argu,
	PROC(RENEW,ppe_1urenew,t_ineureqew_e nPROC(SETCLIENTID,pe_1uset,lienti ,_ineuset,lienti _e nPROC(SETCLIENTID_CONFIRM, e_1uset,lienti rconfirm, d 1uset,lienti _confirm_e nPROC( OCK,	pe_1urpck,t_ineurpck_e nPROC( OCKT,	pe_1urpckt,t_ineurpckt_e nPROC( OCKU,	pe_1urpcku,t_ineurpcku_e nPROC(ACCESS,	pe_1uacces ,t_ineuacces )e nPROC(GETRTED,ppe_1ugetTR4_,t_ineugetTR4__e nPROC(LOOKUP,	pe_1urpokuu,t_ineurpokuu_e nPROC(LOOKUP_ROOT,pe_1urpokuu_root,_ineurpokuu_rootu,
	PROC(REMOVt,ppe_1uremnc_,t_ineuremnc_u,
	PROC(RENAME,ppe_1urenst ,t_ineureqst _e nPROC(LINK,	pe_1urink,t_ineurink_e nPROC(SYMLINK,	pe_1us_rqst ,t_ineus_rqst _e nPROC(CREATE,ppe_1ucreat ,t_ineucreat _e nPROC(PATHCONF,ppe_1upathconf,t_ineupathconfue nPROC(STATFS,	pe_1uste_f ,t_ineuste_f u,
	PROC(RE&h   F,ppe_1uret qst ,t_ineuret qst u,
	PROC(RE&hDIR,	pe_1uret dir,t_ineuret dir_e nPROC(SERVER_CAPS,pe_1userati_cap ,_ineuserati_cap _e nPROC(DELEGRETURN,pe_1udelegtatic ,_ineudelegtatic )e nPROC(GETRCL,ppe_1ugetTcl,t_ineugetTcl_e nPROC(SETRCL,ppe_1usetTcl,t_ineusetTcl_e nPROC(FS_LOCATIONS,pe_1ufs_rpc
{
	s ,_ineufs_rpc
{
	s u,
	PROC(RELEASE_ OCKOWNED,pe_1ureleaserrpckownea,_ineureleaserrpckownea_e nPROC(SECINFO,ppe_1use, arg,t_ineusec argu,
	PROC(FSID_PRESENT,pe_1ufs d_pto e_b,_ineufs d_pto e_bu,
#_OKdef);
d(CONFIG_tvalV4_1)c	PROC(EXCHANGE_Ih,pe_1uexcs, &h_ d,_ineuexcs, &h_ d_e nPROC(CREATE_SESSION,pe_1ucreat dsess
	s,_ineucreat dsess
	s_e nPROC(DESTROY_SESSION,pe_1udestroydsess
	s,_ineudestroydsess
	s_e nPROC(SEQUENCE,ppe_1use xdnco,t_ineuse xdnco)e nPROC(GET_LEASE_TIat,pe_1uget_leasertit ,_ineuget_leasertit u,
	PROC(RECLAIM_COMPLEtm,pe_1ureclaim_complet ,_ineureclaim_complet )e nPROC(GETDEVICEINFO,pe_1ugetdesicR arg,