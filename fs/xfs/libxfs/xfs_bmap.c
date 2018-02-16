/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_extfree_item.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_bmap_btree.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_trans_space.h"
#include "xfs_buf_item.h"
#include "xfs_trace.h"
#include "xfs_symlink.h"
#include "xfs_attr_leaf.h"
#include "xfs_dinode.h"
#include "xfs_filestream.h"


kmem_zone_t		*xfs_bmap_free_item_zone;

/*
 * Miscellaneous helper functions
 */

/*
 * Compute and fill in the value of the maximum depth of a bmap btree
 * in this filesystem.  Done once, during mount.
 */
void
xfs_bmap_compute_maxlevels(
	xfs_mount_t	*mp,		/* file system mount structure */
	int		whichfork)	/* data or attr fork */
{
	int		level;		/* btree level */
	uint		maxblocks;	/* max blocks at this level */
	uint		maxleafents;	/* max leaf entries possible */
	int		maxrootrecs;	/* max records in root block */
	int		minleafrecs;	/* min records in leaf block */
	int		minnoderecs;	/* min records in node block */
	int		sz;		/* root block size */

	/*
	 * The maximum number of extents in a file, hence the maximum
	 * number of leaf entries, is controlled by the type of di_nextents
	 * (a signed 32-bit number, xfs_extnum_t), or by di_anextents
	 * (a signed 16-bit number, xfs_aextnum_t).
	 *
	 * Note that we can no longer assume that if we are in ATTR1 that
	 * the fork offset of all the inodes will be
	 * (xfs_default_attroffset(ip) >> 3) because we could have mounted
	 * with ATTR2 and then mounted back with ATTR1, keeping the
	 * di_forkoff's fixed but probably at various positions. Therefore,
	 * for both ATTR1 and ATTR2 we have to assume the worst case scenario
	 * of a minimum size available.
	 */
	if (whichfork == XFS_DATA_FORK) {
		maxleafents = MAXEXTNUM;
		sz = XFS_BMDR_SPACE_CALC(MINDBTPTRS);
	} else {
		maxleafents = MAXAEXTNUM;
		sz = XFS_BMDR_SPACE_CALC(MINABTPTRS);
	}
	maxrootrecs = xfs_bmdr_maxrecs(sz, 0);
	minleafrecs = mp->m_bmap_dmnr[0];
	minnoderecs = mp->m_bmap_dmnr[1];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++) {
		if (maxblocks <= maxrootrecs)
			maxblocks = 1;
		else
			maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	}
	mp->m_bm_maxlevels[whichfork] = level;
}

STATIC int				/* error */
xfs_bmbt_lookup_eq(
	struct xfs_btree_cur	*cur,
	xfs_fileoff_t		off,
	xfs_fsblock_t		bno,
	xfs_filblks_t		len,
	int			*stat)	/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
}

STATIC int				/* error */
xfs_bmbt_lookup_ge(
	struct xfs_btree_cur	*cur,
	xfs_fileoff_t		off,
	xfs_fsblock_t		bno,
	xfs_filblks_t		len,
	int			*stat)	/* success/failure */
{
	cur->bc_rec.b.br_startoff = off;
	cur->bc_rec.b.br_startblock = bno;
	cur->bc_rec.b.br_blockcount = len;
	return xfs_btree_lookup(cur, XFS_LOOKUP_GE, stat);
}

/*
 * Check if the inode needs to be converted to btree format.
 */
static inline bool xfs_bmap_needs_btree(struct xfs_inode *ip, int whichfork)
{
	return XFS_IFORK_FORMAT(ip, whichfork) == XFS_DINODE_FMT_EXTENTS &&
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORK_MAXEXT(ip, whichfork);
}

/*
 * Check if the inode should be converted to extent format.
 */
static inline bool xfs_bmap_wants_extents(struct xfs_inode *ip, int whichfork)
{
	return XFS_IFORK_FORMAT(ip, whichfork) == XFS_DINODE_FMT_BTREE &&
		XFS_IFORK_NEXTENTS(ip, whichfork) <=
			XFS_IFORK_MAXEXT(ip, whichfork);
}

/*
 * Update the record referred to by cur to the value given
 * by [off, bno, len, state].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_bmbt_update(
	struct xfs_btree_cur	*cur,
	xfs_fileoff_t		off,
	xfs_fsblock_t		bno,
	xfs_filblks_t		len,
	xfs_exntst_t		state)
{
	union xfs_btree_rec	rec;

	xfs_bmbt_disk_set_allf(&rec.bmbt, off, bno, len, state);
	return xfs_btree_update(cur, &rec);
}

/*
 * Compute the worst-case number of indirect blocks that will be used
 * for ip's delayed extent of length "len".
 */
STATIC xfs_filblks_t
xfs_bmap_worst_indlen(
	xfs_inode_t	*ip,		/* incore inode pointer */
	xfs_filblks_t	len)		/* delayed extent length */
{
	int		level;		/* btree level number */
	int		maxrecs;	/* maximum record count at this level */
	xfs_mount_t	*mp;		/* mount structure */
	xfs_filblks_t	rval;		/* return value */

	mp = ip->i_mount;
	maxrecs = mp->m_bmap_dmxr[0];
	for (level = 0, rval = 0;
	     level < XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK);
	     level++) {
		len += maxrecs - 1;
		do_div(len, maxrecs);
		rval += len;
		if (len == 1)
			return rval + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) -
				level - 1;
		if (level == 0)
			maxrecs = mp->m_bmap_dmxr[1];
	}
	return rval;
}

/*
 * Calculate the default attribute fork offset for newly created inodes.
 */
uint
xfs_default_attroffset(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	uint			offset;

	if (mp->m_sb.sb_inodesize == 256) {
		offset = XFS_LITINO(mp, ip->i_d.di_version) -
				XFS_BMDR_SPACE_CALC(MINABTPTRS);
	} else {
		offset = XFS_BMDR_SPACE_CALC(6 * MINABTPTRS);
	}

	ASSERT(offset < XFS_LITINO(mp, ip->i_d.di_version));
	return offset;
}

/*
 * Helper routine to reset inode di_forkoff field when switching
 * attribute fork from local to extent format - we reset it where
 * possible to make space available for inline data fork extents.
 */
STATIC void
xfs_bmap_forkoff_reset(
	xfs_inode_t	*ip,
	int		whichfork)
{
	if (whichfork == XFS_ATTR_FORK &&
	    ip->i_d.di_format != XFS_DINODE_FMT_DEV &&
	    ip->i_d.di_format != XFS_DINODE_FMT_UUID &&
	    ip->i_d.di_format != XFS_DINODE_FMT_BTREE) {
		uint	dfl_forkoff = xfs_default_attroffset(ip) >> 3;

		if (dfl_forkoff > ip->i_d.di_forkoff)
			ip->i_d.di_forkoff = dfl_forkoff;
	}
}

/*
 * Debug/sanity checking code
 */

STATIC int
xfs_bmap_sanity_check(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	int			level)
{
	struct xfs_btree_block  *block = XFS_BUF_TO_BLOCK(bp);

	if (block->bb_magic != cpu_to_be32(XFS_BMAP_CRC_MAGIC) &&
	    block->bb_magic != cpu_to_be32(XFS_BMAP_MAGIC))
		return 0;

	if (be16_to_cpu(block->bb_level) != level ||
	    be16_to_cpu(block->bb_numrecs) == 0 ||
	    be16_to_cpu(block->bb_numrecs) > mp->m_bmap_dmxr[level != 0])
		return 0;

	return 1;
}

#ifdef DEBUG
STATIC struct xfs_buf *
xfs_bmap_get_bp(
	struct xfs_btree_cur	*cur,
	xfs_fsblock_t		bno)
{
	struct xfs_log_item_desc *lidp;
	int			i;

	if (!cur)
		return NULL;

	for (i = 0; i < XFS_BTREE_MAXLEVELS; i++) {
		if (!cur->bc_bufs[i])
			break;
		if (XFS_BUF_ADDR(cur->bc_bufs[i]) == bno)
			return cur->bc_bufs[i];
	}

	/* Chase down all the log items to see if the bp is there */
	list_for_each_entry(lidp, &cur->bc_tp->t_items, lid_trans) {
		struct xfs_buf_log_item	*bip;
		bip = (struct xfs_buf_log_item *)lidp->lid_item;
		if (bip->bli_item.li_type == XFS_LI_BUF &&
		    XFS_BUF_ADDR(bip->bli_buf) == bno)
			return bip->bli_buf;
	}

	return NULL;
}

STATIC void
xfs_check_block(
	struct xfs_btree_block	*block,
	xfs_mount_t		*mp,
	int			root,
	short			sz)
{
	int			i, j, dmxr;
	__be64			*pp, *thispa;	/* pointer to block address */
	xfs_bmbt_key_t		*prevp, *keyp;

	ASSERT(be16_to_cpu(block->bb_level) > 0);

	prevp = NULL;
	for( i = 1; i <= xfs_btree_get_numrecs(block); i++) {
		dmxr = mp->m_bmap_dmxr[0];
		keyp = XFS_BMBT_KEY_ADDR(mp, block, i);

		if (prevp) {
			ASSERT(be64_to_cpu(prevp->br_startoff) <
			       be64_to_cpu(keyp->br_startoff));
		}
		prevp = keyp;

		/*
		 * Compare the block numbers to see if there are dups.
		 */
		if (root)
			pp = XFS_BMAP_BROOT_PTR_ADDR(mp, block, i, sz);
		else
			pp = XFS_BMBT_PTR_ADDR(mp, block, i, dmxr);

		for (j = i+1; j <= be16_to_cpu(block->bb_numrecs); j++) {
			if (root)
				thispa = XFS_BMAP_BROOT_PTR_ADDR(mp, block, j, sz);
			else
				thispa = XFS_BMBT_PTR_ADDR(mp, block, j, dmxr);
			if (*thispa == *pp) {
				xfs_warn(mp, "%s: thispa(%d) == pp(%d) %Ld",
					__func__, j, i,
					(unsigned long long)be64_to_cpu(*thispa));
				panic("%s: ptrs are equal in node\n",
					__func__);
			}
		}
	}
}

/*
 * Check that the extents for the inode ip are in the right order in all
 * btree leaves.
 */

STATIC void
xfs_bmap_check_leaf_extents(
	xfs_btree_cur_t		*cur,	/* btree cursor or null */
	xfs_inode_t		*ip,		/* incore inode pointer */
	int			whichfork)	/* data or attr fork */
{
	struct xfs_btree_block	*block;	/* current btree block */
	xfs_fsblock_t		bno;	/* block # of "block" */
	xfs_buf_t		*bp;	/* buffer for "block" */
	int			error;	/* error return value */
	xfs_extnum_t		i=0, j;	/* index into the extents list */
	xfs_ifork_t		*ifp;	/* fork structure */
	int			level;	/* btree level, for checking */
	xfs_mount_t		*mp;	/* file system mount structure */
	__be64			*pp;	/* pointer to block address */
	xfs_bmbt_rec_t		*ep;	/* pointer to current extent */
	xfs_bmbt_rec_t		last = {0, 0}; /* last extent in prev block */
	xfs_bmbt_rec_t		*nextp;	/* pointer to next extent */
	int			bp_release = 0;

	if (XFS_IFORK_FORMAT(ip, whichfork) != XFS_DINODE_FMT_BTREE) {
		return;
	}

	bno = NULLFSBLOCK;
	mp = ip->i_mount;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	block = ifp->if_broot;
	/*
	 * Root level must use BMAP_BROOT_PTR_ADDR macro to get ptr out.
	 */
	level = be16_to_cpu(block->bb_level);
	ASSERT(level > 0);
	xfs_check_block(block, mp, 1, ifp->if_broot_bytes);
	pp = XFS_BMAP_BROOT_PTR_ADDR(mp, block, 1, ifp->if_broot_bytes);
	bno = be64_to_cpu(*pp);

	ASSERT(bno != NULLFSBLOCK);
	ASSERT(XFS_FSB_TO_AGNO(mp, bno) < mp->m_sb.sb_agcount);
	ASSERT(XFS_FSB_TO_AGBNO(mp, bno) < mp->m_sb.sb_agblocks);

	/*
	 * Go down the tree until leaf level is reached, following the first
	 * pointer (leftmost) at each level.
	 */
	while (level-- > 0) {
		/* See if buf is in cur first */
		bp_release = 0;
		bp = xfs_bmap_get_bp(cur, XFS_FSB_TO_DADDR(mp, bno));
		if (!bp) {
			bp_release = 1;
			error = xfs_btree_read_bufl(mp, NULL, bno, 0, &bp,
						XFS_BMAP_BTREE_REF,
						&xfs_bmbt_buf_ops);
			if (error)
				goto error_norelse;
		}
		block = XFS_BUF_TO_BLOCK(bp);
		XFS_WANT_CORRUPTED_GOTO(
			xfs_bmap_sanity_check(mp, bp, level),
			error0);
		if (level == 0)
			break;

		/*
		 * Check this block for basic sanity (increasing keys and
		 * no duplicate blocks).
		 */

		xfs_check_block(block, mp, 0, 0);
		pp = XFS_BMBT_PTR_ADDR(mp, block, 1, mp->m_bmap_dmxr[1]);
		bno = be64_to_cpu(*pp);
		XFS_WANT_CORRUPTED_GOTO(XFS_FSB_SANITY_CHECK(mp, bno), error0);
		if (bp_release) {
			bp_release = 0;
			xfs_trans_brelse(NULL, bp);
		}
	}

	/*
	 * Here with bp and block set to the leftmost leaf node in the tree.
	 */
	i = 0;

	/*
	 * Loop over all leaf nodes checking that all extents are in the right order.
	 */
	for (;;) {
		xfs_fsblock_t	nextbno;
		xfs_extnum_t	num_recs;


		num_recs = xfs_btree_get_numrecs(block);

		/*
		 * Read-ahead the next leaf block, if any.
		 */

		nextbno = be64_to_cpu(block->bb_u.l.bb_rightsib);

		/*
		 * Check all the extents to make sure they are OK.
		 * If we had a previous block, the last entryf we had a previous block, the last entaddr0(thisp
hfortleaf blo->blkno) {
		er_rec_t		last = {t pointer 
 */
STATstruct xfs_inos_attr_le;
		if (len == 1)
			_leaxfs_attr3_leaf_read(sta_t	n)		*prevp, *key 256) .
	 *  +yp;

		/*
	ta_t	n)		*prevp, *E, stat);
 .
	 *  ecord 
		/*
	ta_t	n)		*prevp, *key 256) ep to see if	if (root				th 
		 * Rea, block, j,	if (nos_attr_le;
		if (len == 1)
			j +	_leaxff_read(sta_t	n)		*prevp, *key 256) ep  +yp;

		/*
	ta_t	n)		*prevp, *E, stat);
 ep  ecord 
		/*
	ta_t	n)		*prevp, *key 256) 	if (;
			}
_inos	if (X see 
ec_t		*ne {0,axfsp, X
		 * ReadULL, bp);
		}
	}

	/*
	 * Here with bp and block set to the leftmost leaf nANITY_Cet_numrecs( If we hs blo'g witvel.
ious bndute opk, i, sz);
		eNITY_sb_agcount);
	reasing keys a(mp, bno));
		if (!bp) {
			bp_release = 1;
			error = xfs_btree_read_bufl(mp, NULL, bno, 0, &bp,
						XFS_BMAP_BTREE_REF,
						&xfs_bmbt_buf_ops);
			if (error)
				goto error_norelse;
		}
		block = XFS_BUF_TO_BLOCK(bp);
		XFS_WANT_CORRUPTED_GOTO(
			xfs_bmap_sanity_check(mp, bp, l}
LL, bp);
		}
	}

	/*
(mp, bno));
		if ( block set to the leftmost lek offset eys_brels:
 j, i,
					(unsigns inbrels", he inode ip L, bp);
		}
	}
f ( block set to the leftmost lT_CORRUPTED_G:
 j, i,
					(unsignBAD af/* S	xfs_btree_ccks).%	int		masts fo));
				pai_byte}
	}
}

/*_btree_cur_MAXEXOR SOMETHING", he inode ip ffset eyright ordAdd
 */
v
#inctersertot block ks).revious umbeck, t Done owhichfo/*
	 *  file system mount s
#incck)l;	/_t	len)		/* delayed extent length */
{
	int		level;		k);

		/*cntd exte */
	xooot block ;
		xfsl;	/* btr->i_d.di_forma,* current btree block */
	s are equal i	crever_	if (mpl;		k);

		/*idxmap_dmwhichfo/*
	 *structu btree level, fo checkextent */
lock ct xfs_btree_bloce theT_BTREE) {
	DINODE_FMT_DEV &&
	    ip-)
oce theT|=6_to_c
	   ip-REE) {*
	 * Root level must use BMAP_BROO_read(sc;
}
=att= NULLFS;
	A / (fl_f) -
	of(bp_release = 0)d_bufufs[i]dx)
			brdx)< c;
	brdxblo
oc
#inccl;		k);l;	/_ == Xdxt use BMAP_, crever_	ifeyright ordVali[off, bl
 * bteleasihe nebe{
		ffset equto maunt i had vali[ ordturn ious urever' t y ainal paramefs_s*
 *pecifiurevct xfs_ou shoulk sge t Done offset equihe neWANTnentryf l
 * by
STvctwhichbmapyon[ ordne oturn iparamefs_sp->t_ite				gotoI_ENTIRE flag was*
	 	int		whichfork)
{
	if (whivali[offse /_t	len)fs_exntst_t/
{
	cur->bc_rec.b.br_startoff = flags,		bp_releasie = 0;

mvalartoff = n(whpp, *thisp, if(wh block addreckextentuctuhe lapist */suct xf_read(sp, if(whthisf(wh bc_bufs[i])
			breakp, if(whADDR(mp, bl_read(smval[i]UP_GE, stat);
}pp = XFl(mp, (flags &e				gotoI_ENTIRE)r3_leaf_read(smval[i]UP_G_rec.b.br>oid
xf;leaf_read(smval[i]UP_GE, stat);
}< XFS_f;leaf_read(smval[i]UP_G_rec.b.br+ mval[i]UP_GE, stat);
}< yp;

		/*
		ITY+XFS_f;lea
	ASSERT(off_read(smval[i]UP_G_rec.b.br<		ITY+XFS_f;leaf_read(smval[i]UP_G_rec.b.br+ mval[i]UP_GE, stat);
}>yp;

		/*
		ITt leaf nA_read(sixr[level !

		/*
	mval[i	ret]UP_G_rec.b.br+ mval[i	ret]UP_GE, stat);
}
= !

		/*
	mval[i]UP_G_rec.b.bt lea_read(smval[i]UP_G_rec.xfs_bm!= DELAYwhiRTck(mpbli_buf;
	*
	mval[i]UP_G_rec.xfs_bm!= HOLEwhiRTck(mpt lea_read(smval[i]UP_G_ret			returnEXT_NORMvel !

		/*
	mval[i]UP_G_ret			returnEXT_UNWRITTEN)_bmap_sa#if (*#deft(
	e cursor or null */
	xfs_ino= 1;
ust use BMAP_Bturn { } p_relea0)*#deft(
	
	if (whivali[offse /_/
{
_staflags,mvalaon(whpf(wh b#chb(mpxtetree_h of a bmap */
vnder l;	/*m}
	podesff, ximum depth of a bmapAdd
ne owhichfo Loop ov;	/*ntries, is bmap_nnder l
 *k seaum de bndror.
 * ov;	/**/
mak aak equso_exte(by		pp = XFS_BM) file system mount sadd/*
 *(ate)
{
	union xfs_btr or ats		pp = XFS_BM*ntries, iunt;
	maxrecs = mp-r_sta or arecs;	/ntries, iunt;
	max*/

/*
 * 0;

fv;	/a or ar;	/*ntries, is e64			*pp;	/* pointe)e */

	mp =ct xf	xfs_bmbt_rec_{;
	max*/

/*
 * (i = tore ickextetent in  	if )	ASem, iunt;
	max*/

/*
 * (i = tornewckextenew	ASem, iunt;
	max*/

/*
 * (i = tor lasckexte last entASem, iunt;t xfs_btree_c
	maxagXFS_BM poiagXo;c
	maxagunion xfsaguXo;co) < mp->m_sb.sb_agcount);
	ASSERT(XFSK) -pp = XFERT(XFSK) -< {
		maxLEN)_bmERT(XFS!is		wh_rec.xfs_b(read_bufagXo
	 * Ro_TO_AGBNO(mp, bno) <bufagbXo
	 * Ro_TO_AGBNOB(mp, bno) <bufERT(XFSagXo
mp->m_sb.sb_agblocks);

	/*
	 * agbXo
f level is reached, folloFERT(XFSK) -< level is reached, folloFERT(XFSagbXo
+XFS_xblocevel is reached, follo#chb(m
f_read(sta_t	n

/*
 * Compute ab.sb_agclloFnew	= helper funude "sta_t	n

/*
 * Compute a, KM_SLEEPlloFnew->xbfirn xfs_btree_lookup(new->xbfirE, stat);
}

(l;		k);len 0)FS_DATufs[itent =t_buf_oFS_F= fv;	/->xbf_t = {;s);
		rFS_F! {
		dmxr;
		rtent =t= 1;
FS_F= *bip;xbfir	if )	bc_bufs[*bip;xbfirn xfs_btree>oid
xfs_ch cur->bc}
LL, btent) if thep;xbfir	if Y_Cetw>bcif (*thfv;	/->xbf_t = {Y_Cetw>bcnew->xbfir	if Y_Ce ic
hfv;	/->xbf_at);
++eyright ordRemoveious bnoint"*
 *"uto ma	lastder _BUF v;	/*
 Pent ct xfso Loop tentslast entAnoin, unl
	xf"*
 *"u lid_t _cpu( Done ov;	/*file system mount sdel/*
 *(ate)
{*/

/*
 * 0;

fv;	/a r atder _BUF v;	/ _cput		level;		*/

/*
 * (i = tor las,exte last ent_BUF de v;	/ahtsib);	level;		*/

/*
 * (i = tor*
 *)or ar;	/*_BUF bmap_nnderd/* maxim, btent) if thep;xbfir	if Y_Cnderp;xbfir	if >bcif (*thfv;	/->xbf_t = {Y_Cnderp;xbfir	if >bcfv;	/->xbf_at);
-->bchelper fun*
 *(ta_t	n

/*
 * Compute a, *
 *)eyright ordoor, upib);	, &curver  ;
		xfsl;	/ file system mount stancel(ate)
{*/

/*
 * 0;

fv;	/)or ar;	/*ntr	n

/*
 * Compsrec_{;
	max*/

/*
 * (i = tor*
 *; r atder r;	/*_BUF nt;
	max*/

/*
 * (i = tornexNO(mp, ipfv;	/->xbf_at);
y (increaS_IFORK_P_read(sfv;	/->xbf_t = {Y.sb_agclloFufs[itder = fv;	/->xbf_t = {;atder;atderY_Cet_n)	bc_b	if Y_Cnderp;xbfir	if >bc	m mount sdel/*
 *(fv;	/at_buf_o*
 *)eyc}
L_read(sfv;	/->xbf_at);
y (inceyright ordIt */
lock efault_m}
	podesff, ximum depth of a bmapTk se	er_ratruct xfs_ino blockec_t	STvcte ab
	 */
	fo,ork exoop tentt			level;	/*
STATfi  ;
		xfsi
	fo,ore */antree leaves.ino blocror.
Si the typblockck_t	nextbnoal				nter- len,.reviize availabrn is ordturn upi typd
xfs_es.
 */
ruct x a fi = 0pesetoop ove.l.bb_ri,
	xfs_fileoff_tur,
	xfs_fileoff_t		of#include ext	xfs_inode_t		ck set0;

tp,exte*k seaum de {
	int		level;		/* data or attxtent length */
{
	int		level;		*ip,		/* incore inode pointer */
	irk) != XFS*logflags bnxtent */
logg{
		flags tree_block	*block;	/   current btree block */
	xf cuREFERENCED trees_fsblock_t		bno;	/* blocck # oxtet_red/
	xfs_buf_t		*bp;	/* buffer forc "block"t_red/
pp = XFS_BM** error return vcalue */t_red/
pp ='s/
	xfs_e, j;	/* index into the extents list */
	xfs_ifoevel, for checkingnt */
lock rrente64			*pp;	/* pointer to 
	mp =ct xf	xfs_bmbt_rec_t		*ep;	/* pointer trrent extent */
	xfs_bms_fsblock_t		bno;	/* blorck # oxte a fil	xfs_buf_t		*bock = ifp->if_broot;
	/*
	 * Root level must use BMAP_BROO_read(si= NULLFflags &e				IF;
}

/*;

	/*
	 * Go d_IFORK_NEXTENTS(ip, whichfork) <=
			XFS_IFORK_MAXE ip fT_PTR_ADDR macro to get or( i = 1; i <= xfsr_btree_get_numrec
				et or( i = 1; i <= xfsr_btree_get_bmap_dmxr[l		et or( i =bp_releasecs = mputine= NULLFSBLOCK);
	A[1])xr[l		et es);
	bno = be64_to_cpu(*pp);

	ArSSERT(bno != NULLFSBLOCK);
	ASSERcNITY_CHECK(mp, bno), erro*logflags T_BTREt xfs_btree_c
, ip(_BMAP_BTREE_REF,
	or null tro= 1;
cps);
1)|
	    be16_dex ino#chb(m
f_BMAP_BTREE_REF,
						&xfs_bmbttp,
cps);
			icbecs = mgoto error_norelse;
}
		block = XFS_BUF_TBLOCK(bp);
		  be16_dex ino	cxfs_bmap_sanity_check(mp,cost le, ip(_BMAP_BTREE_REF,
	or nulxfs_b(= 1;
cpSERT(b0;
cpp)|
	    be16_dex ino	m mount sadd/*
 *(cps);
1,e if the private.b.fv;	/atmst le,ecking codned, fo-->bc_t		ck setmod_d.h"
K);nt (tp;
ust _sanTRAN
		Q_BCOUNT, -1L)>bc_t		ck setbinst (tp,
cpst le, ip if the bp is0]xr[lcpst	   if the bp is0]xr{
		dmxr_ifoeBLOCK			de "sust -1t use BMAP_BROO_read(si= NULLFSBLOCY_sb_agcBROO_read(ssi= NULLFflags &e				IF64_to)y (incey referred tFORKSEENTS(ip, whichf,k) >
			XFS_IFORK_MAXEXerro*logflags T_BreferLOG (bpE |			offh"
#i	xf(use BMAP_BROO*
xfs_bmapat will be nts_e/antree lea-fs_ino blockre */ail	xfs-es.ino blocror.
Tlock-w blockecevi avaian a file, he(;
		xfsi
	fo)i = 0 canngockt_red/
pp =,
	xfs_fileoff_tur,

	xfs_fileoff_t		of#inree leak->bbt
 *(ate)
{ck set0;

tp,eexte*k seaum de {
	int		level;		/* data or attextent length */
{
	int		level;		/buffer for*t = {pSERT( to blip's_btree	de "ode	*nt;
	max*/

/*
 * 0;

fv;	/a or ah "len"nderd/;
	xaum de level;		*ip,		/* incorre iattexter */
	iffset equ extrever tree_block	asdeld exte *nts_e{
		aber */
	i	de "irk) != XFS*logflags b	xtent */
logg{
		flags tree_block	*block;	/* current btree block */
	xfs_fsblock_t		bno;	/* bloack # of "b	de "ode	*(t_red)il	 */
	int			bp_reeturn vabeckexte
	xfs_extnua*/
	int			bp_r	de "_arg xfsargsckexte	de "od de argum, is e64			*pelease = 0;

areckextet_red//*
	 *s{
	int		leves_fsblock_t		bno;	/* block # ofode pointek */
	int		minnl;		*ip,		/* incore iofode p*/
void
xer */
	irk) 		*pelease = ho_allf {0, p_dmwhichfo/*
	 *sct xfs_btree_blockdex intto the extents list */
	xfs_ifork_t		*ifp;, c;
	ap_dmwhichfo/*
	 *structu btree level, foo checkextent */
lock ct xfs_btreeb_level) > 0);

	kpnts in a file, he> 0 ct xfs_btreeb_lep;	/* pointer  */

	mp = ip->i_mount;
	maxrk_t		*ifp* Note tof "bXFS_BM*ntrblockck_t	nextreeb_level)  tr);

	ppnts in a file, het */
	xfct xfs_btreock = ifp->if_broot;
	/*
	 * Root level must use BMAP_BROO_read(s
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORK_MAXEXllowing the M void
xfs_;
		xfsi
	foent lenum_recs;_ifoeBLOCK			de "sust 1t use BMAP_BROOi= NULLFflags |=e				IF64_tolowing the Fthis filesyBLOCum_recs;T_PTR_ADDR macro to get , ip_ifosb inode d_hascrc(&cevel is)
f ( blo*ip,		ini *E, st	inten == 1)
			_sanity_xfs_b__buf_lse;
s = mgoto AGIC))
		t 1t 1ine to ri
{
	ce;
s = mgrror_LONGvel S |	 = mgrror_AGICck(mpXerroif (*th blo*ip,		ini *E, st	inten == 1)
			_sanity_xfs_b__buf_lse;
s = mgoto ))
		t 1t 1ine to ri
{
	ce;
s = mgrror_LONGvel Sllowing the Neis pro */
	.  Can'tb	de "oder (leftget_numr omputelrd/;
um_recs;FS_F= bp_releasini *o */
	_bmbttp,
ust use BMAP_BROO if the private.b.f = {pSERT*ne f = {pSERTROO if the private.b.fl;	/*= fv;	/ROO if the private.b.flags = 	asdel ?s = mgrCUR_BPRV_WASDEL :		if ng the e nts_e/ */ail	xfsrec_t		woCheck A[1e ab/*
	 *stryBLOCum_recs;referred tFORKSEENTS(ip, whichf,k) >
			XFS_IFOR_MAXE ip mem->i_&args(b0;
 -
	of(argsd_bufargs. (nos (X sargs.k = im(X sargs.f = {pSERT*ne f = {pSERTROO(%d) f = {pSERT*nsb_agcount);
		bc_bargs. )
			EV &&
Lt);TYPE_whiRT_BNO;c_bargs./buXo
	 * Ro		X_cheounoutine to ri) <buf
	ASSER, ipfv;	/->xbf_low		bc_bargs. )
			EV &&
Lt);TYPE_whiRT_BNO;c_bargs./buXo
	  f = {pSERTROO
	ASSERT(ofargs. )
			EV &&
Lt);TYPE_NEAR_BNO;c_bargs./buXo
	  f = {pSERTROO
 sargs.k(maxn
	 args.ks_bmn
	 args.prod						Xargs.	asdel = 	asdelrro*logflags T_BTREe, ip(_BMAP_BTREE_	de "_vck_t	n_&args))m_recs = xeBLOCK			de "sust -1t use BMAP_BROOh blo*ip,		del/o */
	_= 1;
			egrror_ERRORXLEVEL be16_dex ino	}f ng the Ade "od de can'tbb.br,i typd
xfs_was*rribute it_recs;ERT(XFSargs./buXo
.sb_agcount);
	ASSERT(XFS f = {pSERT*nsb_agcount);
vel != 0]]]]args.agXo
		 * Ro_TO_AGBNO(mp, bn f = {pSERT)vel != 0]]]]pfv;	/->xbf_lowbli_buargs.agXo
> * Ro_TO_AGBNO(mp, bn f = {pSERT)d_buf f = {pSERT*ne if the private.b.f = {pSERT*neargs./buXoROO if the private.b.	de "ode	++eye,ecking codned, fo++eye_t		ck setmod_d.h"
K);nt (tp;
ust _sanTRAN
		Q_BCOUNT, 1L)>bca!bp) {
			 block, i&xfs_bmbttp,
args./buXo[1]);
	ng the Fthis filesyt_red/
pp =,
_recs;a!b thFS_Bp) }
		block = XFS_B>bca!fs_bmap_sanity_check(mp,apst le, ip_ifosb inode d_hascrc(&cevel is)
f ( blo*ip,		ini *E, st	inten ==a 1)
			a!b thFbn
	ce;
 = mgoto AGIC))
		t 0t 0t e to ri
{
	ce;
 = mgrror_LONGvel S |	 = mgrror_AGICck(mpXerroif (*th blo*ip,		ini *E, st	inten ==a 1)
			a!b thFbn
	ce;
 = mgoto ))
		t 0t 0t e to ri
{
	ce;
 = mgrror_LONGvel Sllowiarenos_attr_le;
		if (len ==a 1)
			1lloFnek_t	nexADDR macro ;
	A / (fl_f) -
	of(bp_release = 0)loFufs[ic;
}
 ])
			break* Note toDDR(mp, blebp) {
		i Nock, i	xf(DR pai_bytl(mp, is		wh_rec.xfs_b(bp_releasp, *key 2xfs_b(ep tr3_leafare->l0 (block->bb_64(ep->l0f;leafare->l1 (block->bb_64(ep->l1f;leafare++e c;
++eyeaf nodO_read(sc;
}
=a whichfork);
}

/*
 * Update the )>bc_t		*ip,		s, if any.
	a 1)
			cs);

wing the Fthis filesyBLOCe> 0  = 0pt xfs_,
_recs;kRT(be64_to_cpu(prevp->br_startoff1_bufarenos_attr_le;
		if (len ==a 1)
			1lloFk block numbers (block->bb_64(ta_t	n)		*prevp, *key 256) are)	et es);
	bno =to_cpu(*pp);
		XFS_WANT_CObp_releasp, *ecs = mp
	int						_block(block, mp, 1, ifp->ifd_buf es);
lock->bb_64(args./buXo;

wing the Do.revioulblkogg{
		l
 * btrnquso_default_attroBLOCeis	l
 * bt_extnu first */
		b_t		*ip,		h"
#xfs_b(= 1;
abecs = mgB&
Lt_BIEXerro_t		*ip,		h"
# = mp
	in
abecs_CO_block(blocka_btree_get_bmap_dm	ASSERT(XFS e iaY_sb_agcBROO e iaY_Ce ic
h*logflags T_BreferLOG (bpE |			offh"
#i to g(use BMAP_BROO*
xfs_bmapat will be nts_e/aake spablock */antree leavelocror.
Tlt numfoensvel *ntr	  02k ks).xfs_inode t Doregularpute_m301  si the typblockxfs_il xfs_bmaSERTkoggequso_de{
	ould hakeyye *ns;	/e/* fil (T btelap-/
	leve}
	podesff,extbnoo0(thiough) file system mount ske sp ext	xfs_in_emptynt;
	uint			offset;

	i,ee_block	*block;	/
	xfs_fsblock_tevel,o che
	 * Root level must use BMAP_BROOO_read(s
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORk(mAcBROO_read(sDR macro ;
	A  (incey _read(s
		XFS_IFO);
}

/*
 * Update the r (incey;
	max*/

/*hfork == XFS_ust use BMAP_BROOi= NULLFflags &= ~
		XFSINLINEROOi= NULLFflags |=e				IF;
}

/*ey referred tFORKSEENTS(ip, whichf,k) >
			XFS_IFORK_MAXEXerrat s_fileoff_tur,
	xfs_fileoff_t		of#inke sp ext	xfs_in(ate)
{ck set0;
tp,eexte*k seaum de {
	int		level;		/* data r attextent length */
{
	int		level;		/buffer fo*t = {pSERT( to blip'ile, hetde "ode	*;
	xaum de level;		k);len 0	totald extetotalah "len"l xffs_extnk seaum de tree_bloc*logflags b	xtent */
logg{
		flags tree_blocp, whichf,
	syst		(*ini *fn)em.li_type =nk se 
tp,	ce;
s r,
	xfs_fsblock_tbp,	ce;
s r,
	xfs_fsblchfork) ==	ce;
s r,
	xfs_fsblclock *i= ) block add_BMAP_BTTREe, addflagsnts inlogg{
		flags ffset equ btree level, fo checkextent */
lock ct xfs_btreebp_r	de "_arg xfargsckexte	de "od de argum, is e64			*peeturn*beckexte
	xfs_extnuwhichfo*/
	int			bp_release = ho_all  {0, 0};whichfo/*
	 *sct xfs_btrewing the Wfor_n'tb XFSilabr		drec_t		las dela Doositions local fs_ieset(
	yeCum_re SBROOndile (levxfs_inode__FORKregularpi
	foenk ;
vali[it_recs;ERT(XFS!(ferSREG(,ecking codm	fo)i&& DINODE_FMT_DEV &&p->m_bmap_)t;
	/*
	 * Root level must use BMAP_BROO_read(s
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORk(mAcBROVELS; iDR macro ;
	Am_recs = xunt ske sp ext	xfs_in_emptynust use BMAP_BROO	flags = referLOG (bpEROO	S_WANde andee 
eflags = TREe_BMAP_BTTREe_read(ssi= NULLFflags &e(
		XFSINLINE|				IF;
}

/*|				IF;
}I
		))}
= !
rror)
				FSINLINE ip mem->i_&args(b0;
 -
	of(argsd_bufargs. (nos (X sargs.k = ifp->if_broot;
args.f = {pSERT*ne f = {pSERTROOng the Ade "odaian
pp =,  Wfoknowbwool xf	STvcte a, si the tyse scelocktent invctfiis contnpi
	foXEXTNUM;
		s f = {pSERT*nsb_agcount);
		bc_bargs./buXo
	 * Ro		X_cheounoargs.k ine to ri) <bufbargs. )
			EV &&
Lt);TYPE_whiRT_BNO;c_
	ASSERT(ofargs./buXo
	  f = {pSERTROOfargs. )
			EV &&
Lt);TYPE_NEAR_BNO;c_
 sargs.totala=etotal; sargs.k(maxn
	 args.ks_bmn
	 args.prod						X_BMAP_BTREE_	de "_vck_t	n_&args)F_TBLOCK(bp);
		S_WANde and*/
	lian'tbb.br,i typd
xfs_was*rribute irecs;ERT(XFSargs./buXo
.sb_agcount);
	ASSERT(XFSargs.K) -
				buf f = {pSERT*neargs./buXoROO!bp) {
			 block, i&xfs_args.k intp,
args./buXo[1]);
OOng the Ini ialisot)
			pp =  = 0ogram(levxfs_ ATTR1 that
	: ini *fnvel = 
	 * 
				xfs_elidp, &c_t), o
	 lengly!EXTNUM;
ni *fn(tp;
bp;
ust i= )nd*/
	lacat);
yes.
 */
ch sge con */
	xleafendelidp
	lryde{
	nt			bp_rck seth"
#xuf(tp;
bp;
0,DDR macro ;
	A -			bufee lexfs_K			de "sust -DR macro ;
	At use BMAP_BROO = xunt ske sp ext	xfs_in_emptynust use BMAP_BROOflags |=e				ILOG (bpEROufee le Nocadd(DR pa0		1lloFebp) {
		i Nock, i	xf(DR pa0BROO = xuneas_update(ce pa0		args./buXo[11,eturnEXT_NORMBROO
#inccl;		unt spo_al indirei pa0	
ock	*block;	T_DEV &&
	    ip->?6_to_c
	   ip- :			
ock_THI		IP_cey referred t);
}KSEENTS(ip, whichf,k1t le,ecking codned, fo						X_t		ck setmod_d.h"
K);nt (tp;
ust
)
				TRAN
		Q_BCOUNT, 1L)>bcflags |=e		offh"
#i	xf(use BMAP_BRO
de a:
h*logflags T_Bflagsn
EL be16_dex inoly created ilequto mam mount sadd/ree ck;	TWANh sdletruct xfs_ino bloc
	int		whichfof_tur,

	xfs_fileoff_t		of#inadd/ree ck;	bbt
 *(ate)
{ck set0;

tp,eexte*k seaum de {
	int		level;		/* data or attextent length */
{
	int		level;		/buffer for*t = {pSERT( to blip'ile, hetde "ode	*nt;
	max*/

/*
 * 0;

fv;	/a or ah "len"WANnder l
 commifork) != XFS*flags)		xtent */
logg{
		flags tre{nnl;		*ip,		/* incore iofode pointer */
	irk) != XFSdex intto the extents list */
	xfs_ifop;	/* pointer  */
block address */
	xfs_bmirk) != XFS_retckextenewBLOCe_retusbtreock = ifp->if_broot;
	/ (,eckingf.LLFSBLOCK);
	Axbloreferred tDSIZE_d.d;
		*flags |=e				ILOG D64_tolo	ASSERT(ofFS_F= bp_releasini *o */
	_bmbttp,
ust div(len, maxrecs)O if the private.b.fl;	/*= fv;	/ROOO if the private.b.f = {pSERT*ne f = {pSERTROOe, ip(_BMAP_BTREE_Rff,
	xfs_fsblo
	in
0n
0n
0n
&btreed;
		FS_WANT_COR	if (*/

l = br l
 	}
	t1e abbnoint sz);0);
		if (bp_release) {
btre-
			ns_brelse(NULL, b(_BMAP_BTREE_REF,
	newxeBLOCo
	in
flags,
&btreed;
		FS_WANT_COR	if (L, bbtre-
		bp = xfh blo*ip,		del/o */
	_= 1;
			egrror_NOERRORXLEVEEL be16_-ENOSPCeyeaf nf f = {pSERT*ne if the private.b.f = {pSERTROOO if the private.b.tde "ode	*
		if ( blo*ip,		del/o */
	_= 1;
			egrror_NOERRORXLEV}OO*
xfs_bmap_brels:
 j, i*ip,		del/o */
	_= 1;
			egrror_ERRORXLEVL be16_dex inoly created ilequto mam mount sadd/ree ck;	TWANh sdletree leaves.ino bloc
	int		whichfof_tur,

	xfs_fileoff_t		of#inadd/ree ck;	b	xfs_inode_t		ck set0;

tp,eexte*k seaum de {
	int		level;		/* data or attextent length */
{
	int		level;		/buffer for*t = {pSERT( to blip'ile, hetde "ode	*nt;
	max*/

/*
 * 0;

fv;	/a or ah "len"WANnder l
 commifork) != XFS*flags)		xtent */
logg{
		flags tre{nnl;		*ip,		/* incore iofode p*/
void
xer */
	irk) != XFSdex intto the extents list */
	xf;
	/ (,ecking.y di_anexte1  si
	of(bp_release = 0)xbloreferred tDSIZE_d.d;
		*
xfs_bmapfFS_F= 
		dmxr_BMAP_BTREE_Rf#inree leak->bbt
 *(tp,
ust t = {pSERT( fv;	/at&
	in
0nOO	flagst div(len, maxrecs), ip if)RT(ofFS_ the private.b.tde "ode	*
		if ( blo*ip,		del/o */
	_= 1;	XFS_BMAP_?
			egrror_ERROR :				egrror_NOERRORXLEV}OO*
xfs_bdex inoly created ilequto mam mount sadd/ree ck;	TWANh sdletke spabs.ino bloc
	 Each
reforxfs_chfoxfs_inode_umbeck,_t), ol xfs_aforxfs_chfotrevel *labrn op tent *nts_de d. SomR(mp, 
		 */endeSTvctrequirypd
ecialah "le ini ialisod detent revel aves.
 */
xfs_inodino{
	, o_ADDs (f lengolock)(mp, sopd
ecialisod
ne ytenth sdletr	lryde{
	nne m->le_cur_ttentXXX (fgc): ine_ctigodaiwheTIC if lengolyt *nts_de dork ousot)
		gite icfilblksinoe{
		 revel . Itl xfs_bmap_s.
 */
ST-p, 's jl = a 	lry complexfilblksinoeer,
	xfs_fileoff_tur,

	xfs_fileoff_t		of#inadd/ree ck;	bke spode_t		ck set0;

tp,eexte*k seaum de {
	int		level;		/* data or attextent length */
{
	int		level;		/buffer for*t = {pSERT( to blip'ile, hetde "ode	*nt;
	max*/

/*
 * 0;

fv;	/a or ah "len"WANnder l
 commifork) != XFS*flags)		xtent */
logg{
		flags tre{nnl;	ncluargs 0;
dargsckexte	rgs ks).xir/ree bumfoe	xf;
	/ (,eckingf.LLFS;
	Axbloreferred tDSIZE_d.d;
		  be16_to_cpu(blferSDIR(,ecking codm	fo)NDBTPTRem->i_&dargs(b0;
 -
	of(dargsd_buf
dargs.geo= ifp->if_broovel xirsblobuf
dargs.d = ifpbuf
dargs.f = {pSERT*nef = {pSERTROOOdargs.fl;	/*= fv;	/ROOOdargs.totala=edargs.geo->/bucbroot;
Odargs.	*block;	T_ div(len, maxrROOOdargs.tk se os (X srst-case numde "_sfk->bbfs_b(&dargs)ndee 
eu(blferSLNK(,ecking codm	fo)N srst-case numof#inke sp ext	xfs_in(tp,
ust t = {pSERT( 1nt						 flagst div(len, maxrnt						 _ifos
#inclnke sp extremohe wo*/
	l xfs_bmSTvctbs urevedves.
 ), n".
 */supporttke spabs.ino rrente64	ERT(XFS0XLEVL be16_-xfs_btree_cuapat will be nts_e/nt */
lo manon-re
 * posqu exre
 * posq fil Ml = no= br contr*k seaum de,
us

l = no= br SERTsq fil/
f_tur,


	xfs_fileumfoe	xf_t		of#inadd/ree ck;	(vel;		/* data or attextent length */
{
	int		leve!= XFS_-
	ttexted
xfs_k-w re
 * possl xfs_leve!= XFSrsvd)		xtexaum mayousotrribute  s =  tre{nnl;	n/buffer forf = {pSERTR	xte1p'ile, h/agetde "ode	*nt;
	max*/

/*
 * 0;
fv;	/R	 r atderdowhichfo/*
	 * btreeb_lep;	/* pointer  */

	mp = ip->i_mount;
	maxck set0;

tp;eexte*k seaum de {
	int		leve!= XFS_lksckexted
xfs_rributam de tree_bloc	ts_de do				extedupefT_PTR_ree bts_de dotree_bloc	commifde	;	xtexaume dowas*commifde	otree_bloc	logflags;s inlogg{
		flags rk) != XFSdex intto the extents list */
	xfs_bloc	cancel_flags = TREOO_read(s
		XFS_IFOQ_d.di (incey;
k = ifp->if_broot;
ERT(XFS!
		XNOT_DQATTACHEDoutine d_buftbp) {
		ck set	de "srecs = mTRAN
	ADDAmaxrecs)s =  DEV &&
DDAmaxr(mp, ipREaxreecs), iprsvd)
oc
ip = flags |=e				TRAN
	REaERVEmxr_BMAP_BTREE_clude "xfbute(tp,
&MpREaxreep =rnaddaichf,ks = pa0BROOBLOCK(bp);_recs = xclude tancel(t pa0BROOEL be16_dex ino	}f cancel_flags = 				TRAN
	RELEASr_LOGpREabufee lefs_b(ust div(It);
_EXCL)>bc_BMAP_BTREE_clude "xfbutem.h"
#dnedkn(tp,
ust s = pa0, rsvd ?
r)
				QMOPe;
	
	REGBLKS |	 = mQMOPe;max ipREa :
r)
				QMOPe;
	
	REGBLKS)F_TBLOCK(bp);
		S_WANclude tancel;f cancel_flags |=e				TRAN
	ABORTF_TBLOC
		XFS_IFOQ_d.d;
		S_WANclude tancel;f 	/ (,ecking.y daefault_attroffset(ip) >> K_MAXEXe = xfs_
 we hFore we coucom{
		fo mapre-6.2pute_maxlevck, i, sz);_read(sDecking.y daefault_ (incey 	Decking.y daefault_ troffset(ip) >> K_MAXEX; nodO_read(sDecking.y danek_t	nexA(incey;
	maxclude ij
	i(tp,
ust 0BROO = xck seth"
# we c(tp;
ust _sanILOG (bpEcey;
 reset (,ecking.y defaulte = x delaroffset(ip) >> DEV:y 	Decking.y de
 */

STAr  02up(si
	of(bp_rdevtnum_8di_forkoch cur->bc delaroffset(ip) >> uint:y 	Decking.y de
 */

STAr  02up(si
	of(uuxfs_um_8di_forkoch cur->bc delaroffset(ip) >> k(mAc:bc delaroffset(ip) >> K_MAXEX:bc delaroffset(ip) >> grror:y 	Decking.y de
 */

STAREE_	ee _pointefauFS;
	AfiS_ust si
	_bytl(mp, i

/*
 * Debug/sanity checking code
 */

STAbp_rdeorkoff)
			ip->i_d.di_forko		if ( , ip->i_d.flags &e				MOUNTc
	  2ity cts_de do		2koch cur->bcdeorkof:z);_read(sncey 	_BMAP_BT-EINVAL;
		S_WANclude tancel;f turn offsetheckinafaY_sb_agcBROOheckinafaY_ helper funzude "sta_tevel, te a, KM_SLEEPlloFheckinafaNULLFflags =e				IF;
}

/*ey logflags*
		if 	max*/

/ini (&fv;	/at&f = {pSERT);;
 reset (,ecking.y defaulte = x delaroffset(ip) >> k(mAc:bcr_BMAP_BTREE_Rf#inadd/ree ck;	bke spotp;
ust &t = {pSERT( &fv;	/aty c&logflags)koch cur->bc delaroffset(ip) >> K_MAXEX:bcr_BMAP_BTREE_Rf#inadd/ree ck;	b	xfs_in(tp,
ust &t = {pSERT(ty c&fv;	/at&logflags)koch cur->bc delaroffset(ip) >> grror:y 	_BMAP_BTREE_Rf#inadd/ree ck;	bbt
 *(tp,
ust &t = {pSERT( &fv;	/aty c&logflags)koch cur->bcdeorkof:z);_BMAP_BTTREeh cur->bc}
LL, blogflags)ecs = xclude h"
# we c(tp;
ust logflags)kocBLOCK(bp);
		S_WANunt stancelkocBLOC!_ifosb inode d_hasree (&cevel is)vel != 0C!_ifosb inode d_hasree 2(&cevel is)v&&bts_de do		XF)NDBTPT_	intCK(m sbformas = TREOO	spin_fs_b(&cevel is_SERT);;
cBLOC!_ifosb inode d_hasree (&cevel is)p = xfh blosb inode d_addaee (&cevel is)LEVEEsbformas |=e				SB_VERSION = xfs_};
cBLOC!_ifosb inode d_hasree 2(&cevel is)v&&bts_de do		XF) = xfh blosb inode d_addaee 2(&cevel is)LEVEEsbformas |=e(				SB_VERSION =  |	 = mSB_FEATU
	
2)xfs_};
cBLOCsbformas) = xfhspin_unfs_b(&cevel is_SERT);;
ceb_lep;d is(tp;
sbformas)xfs_} if (*thispin_unfs_b(&cevel is_SERT);;
turn_BMAP_BTREE_Rf#infinish(&tp,
&fv;	/at&
ommifde	)kocBLOCK(bp);
		S_WANunt stancelkoc_BMAP_BTREE_clude 
ommif(tp;
				TRAN
	RELEASr_LOGpREa	bufee leunfs_b(ust div(It);
_EXCL)>bcL be16_dex ino
unt stancel:f 	max*/

/tancel(&fv;	/)>bclude tancel:
s = xclude tancel(t pacancel_flags	bufee leunfs_b(ust div(It);
_EXCL)>bcL be16_dex inoright ordItnt	nal ende	xfs	nal whichfo der searet ximum dep.th of a bmape64_s filesyies, is bmai/
	xfs_in fil Ahis f */
lormas mp, set upihis rever,bwoojl = cluinode
 */
ruct  wher= 0ogram(lev/*
	 * bin.hs b typblock addrescanno= umbea fiunwrifdententt			lev,m(lev/*
	 * bmp, or nuedves.
no "_ret	"	flags fil/
f_tur,

	xfs_fileoff_t		of#in						xfs_inode_t		ck set0;

tp,exte*k seaum de {
	int		level;		/* data or attxtent length */
tree_block	*block;	/  current btree block */
	xfs_fsblock_t		bno;	/* blopSERTR	xtelock" */
	xfs_buf_t		*bp;	/* buffer for "block" */
	int			error;	/* error return value */
	xfs_extnum_t		i=0, j;	/* index into the extents list */
	xfs_iforkntfm* poirkntfnto tturnEXT >> NO_filEahtsie */
	__be64			*prk_t		*ifp;, fork structure */
	int			level;	/* btree level, for checking */
	xfs_mount_t		*mp;	/* file system mount structure */
	__be64			*pp;	/* pointer to block address */
	xfs_bmbt_rec_t		*ep;	/* pointer to current extent */
	xfs_bm cuREFERENCED tree		*prk_t		*ifproomof "bXFS_BM*ntrt block _ADDR'soBLOmuctur	xf;
p, whichfork);
	block = ifp->if_broot;
	/*
	 * Root level must use BMAP_BROOrkntf
	 	DINODE_FMT!DEV &&p->m_bmap__?
			eEXT >> NO_filE :
r)
)
				EXT >> et(ip(	ifeyOT_PTR_ADDR macro to get ptr out.
	 */
	level = be16_to_cpu(block->bb_level);
	ASSERT(level > 0);
	xfs_check_block(block, mp, 1, ifp->if_broot_bytes);
	pp = XFes);
	bno = be64_to_cpu(*pp);

	ASSERT(bno != NULLFSBLOCK);
	ASSERT(XFS_FSB_TO_AGNO(mp, bn) < mp->m_sb.sb_agcount);
	ASSERT(XFS_FSB_TO_AGBNO(mp, bno) < mp->m_sb.sb_agblocks);

	/*
	 * Go down the tree until leaf level is reached, follo ptr outfirst
	 * pointer (leftmost) at each level.
	 */
	while (level-- > 0) {
		/* See if buf is in cur first */
		bp_release = 0;
		bp = xf_BMAP_BTREE_REF,
						&xfs_bmbttp,
ps);
			if (error = mgoto error_nore }
		block = XFS_B);;
cBLOCK(bp);
		XL be16_dex ino		xfs_bmap_sanity_check(mp, bp, level),
			error0);
		if (level == 0)
			break;

		/*
		 * Check this block for basic sanity (increasing key		bno = be64_to_cpu(*pp);
		XFS_WANT_CORRUPTED_GOTO(XFS_FSB_SANITY_CHECK(mp, bno), error0);
		if (bp_release) {
			bp_release = 0;
			xfs_trans_brelse(NUL_t		ck set to thetp;
bpXLEV}OOin the tree.
	 */
	i = 0;

	/*
	 * Loop over all leaf nodes checking that all eBLOmuADDR macro ;
	A / (fl_f) -
	of(bp_release = 0)loFxtents Oin the e right order.
	 */
	for.  Cgraminnodino{de  */
	int			leo/*
	 *  f_recs;


		num_recs = xelease = 0;*frp(NUL_t		fs_btree_get_numrecs(block);

		/*
		 * Reads(block);

		/*key 2REOO	ad the next leaf block, if any.
		 */

		basic unfikely(i +X
		 * Re
		BLOm)r3_leaf_read(si +X
		 * Re
<=	BLOm);;
ceb_le,
			fp->if_broo(error"
	 lupt dth */
%Lu, (em mout			lev).ts for rs are equal in nodene to ri) <bufbr0);
(bp_relION_ERROR("_t		of#in						xfs_ino1)"(error = mERRUF_AD_LOW,ifp->if_broo	XFS_WA<bufbrS_WANT_COR	if (} level),
			error0);
		if (level == 0)
			break;

		/*
		 * C0this block for batbno = be64_to_cpu(block->bb_u.l.bb_rightsib);

		/*
	 * Check all the extents to make sure they are OK.
	basic  Check a!sb_agcount);
	reasREE_REF,
					a	&xfs_bmbt Check ( 1nt					0]]]]}
		block = XFS_B);;
c		 */
		ifpyv/*
	 * bin */
	int			leo/*
	 *  f_	/*
	 *frenos_attr_le;
		if (len == 1)
			1lloF*key 2uADDloF*	if (root0			th 
		 * Rea, bl,DDR(_o*
pR(mp, bl	bp_release = ho_all  trbp) {
		i Nock, i	xf(DR pai_bytl	tre->l0 (b the extentsfrp->l0f;leaftre->l1 (b the extentsfrp->l1)xfs_};
cBLOCrkntf
	=
			eEXT >> NO_filEmp, bl			 */we had a previre
 * possp*/
void
xe/*
	 * bm, mp,	wher=y "older"urrentp*/
void
xe/*
	 * bxtnuamp,	whe
	 *bi  ;
		xfs"t			leoflag"_s.
io{de.mp,	wh/ty chic unfikely({
		or nulno_ret			xfs_inoDR pt					key 2, 
		 * Re tr3_leafr = mERROR;
	POd(s"_t		of#in						xfs_ino2)"(error;
s = mERRUF_AD_LOW,error;
sfp->if_broof;leafrS_WANT_COR	if (re in thL_t		ck set to thetp;
bpXLEVANITY_Cet_numrecs( If we hs blo'g witvel.
ious bndute opk, i, sz);
		eNITY_sb_agcount);
	reasing keyxf_BMAP_BTREE_REF,
						&xfs_bmbttp,
ps);
			if (error = mgoto error_nore }
		block = XFS_B);;
cBLOCK(bp);
		XL be16_dex ino		xfs_bmap_sanity_check(mp, bp, lodO_read(sD}
=att= NULLFS;
	A / (fl_f) -
	of(bp_release = 0)d_buf_read(sD}
=a whichfork);
}

/*
 * Update the )>bc = mgoto TR, ipEXLISENTS(iit use BMAP_BROO*
xfs_bmap_brels:
 j, ick set to thetp;
bpXLEVL be16_-xfs_btree_cuapat  a bmapSearet 
	int			leo/*
	 * ves.
 */
bnointumbea file xfs_bmps) fil s bNITYlock ;
	a hole,=ct xf	 */
	in(ip, wnoin.  s bNITYlocktentshfortof, *tofpuld hab, set,i = 0	prevpuld haumbea fihisp
hfotenttnoint(		whii*/
	ne).  E th, *
hfoxpuld hab, set	 */
	intructtent Done of  02ttnoin; *S_Wpuld haumbea fihispwnoin.
	xfs_fileofbp_release = ho_all  	nter to currentf  02tt			leobnoint szl == 0)
		earet_multi		xfs_inode_t		evel, fo che,		xtent */
lock ct xfs_btreebp_rfs_exntst__btr or axfs_bmXFS_BM*	earetedves.
tree_bloc*tofpr or ael :trnquntrblockf  02ttree		*prk_t		*if*
hfoxp,or ael :tnter to next euctu btree leleasie = 0;*S_Wpr or ael :tr			leobnointf  02ttree		*peleasie = 0;*   be6 or ael :tslast entA			leobnointf  02ttre{		bp_release = ho_all  {0, p_dmwhichfo/*
	 *sct xfs_btree		*prk_t		*if
hfoxnts inlter to next euctu btOOng the Ini ialize 
	int			leotleaf bfs_bmbt_r extrset acc
	xfto the unini ialize 0;_G_rec.xfs_bmlorma f_recs;S_Wpblock numbers (b0xffa5a5a5a5a5a5a5	dmxrS_Wpblockxfs_bat);
}

0xa55a5a5a5a5a5a5a	dmxrS_Wpblocke theT_BturnEXT_INVALID;s;S_Wpblock numbxfs_bmap0xffffa5a5a5a5a5a5	dmxr   beblock numbers (b_agcoILEOFFREOOebp) {
		i Noc_bt ext	xf(DR paps);
.
	 *x)kocBLOC
	 *x
		bp = xfbp_releasgupdate({
		i Nock, i	xf(DR pa
	 *x
-			,    be6>bc}
LL, bl	 *x
<att= NULLFS;
	A / (fl_f) -
	of(bp_release = 0)d_ = xfbp_releasgupdate(e paS_Wp);;
c*tofpuents O
	ASSERT(ofBLOC
	 *x
		bp = xf;*S_Wp*ne    bexfs_};
c*tofpuen			XFebp) 
		dmxr}
f*
hfoxpp) 
hfoxnbcL be16_dpnoright ordSearet 
	int			level;	/*es.
 */
i
	fo,oes.
 */
b			leoumbea file xs) fil s bNITYlock ;
	a hole,=ct xf	 */
	in(ip, wnoin.  s bNITYlocktshfortof,fil *tofpuld hab, set,i = 0	prevpuld haumbea fihisp
hfottnoint(		whii*/
	ne).fil E th, *
hfoxpuld hab, set	 */
	intructt Done of  02tenttnoin; *S_Wpuld haumbea fihispwnoin.
	xfs_fileofbp_release = ho_all                   ter to currentf  02tt			leobnoint szl == 0)
		earet_	xfs_inode_t		e* data     r at            ternt length */
{
	int		level;		/s_exntst*
		ITt            terxfs_bmXFS_BM*	earetedves.
tree_bl             ichf,kkkkkk* current btree block */
e_bl             *tofpr          terel :trnquntrblockf  02ttree		*prk_t		*i    *
hfoxp,        terel :tnter to next euctu btree leleasie = 0 *S_Wpr          terel :tr			leobnointf  02ttree		*peleasie = 00	prevp)         terel :tslast entA			leobnointf  02ttre{		bp_revel, fo checkextent */
lock ct xfs_btreebp_release = ho_all   {0,            terwhichfo/*
	 *sct xfs_btrewi = mSfilRo		C(xs
	xfsck)l;	/)t;
	/*
	 * Root level must MAP_BROOOebp) {
		 0)
		earet_multi		xfs_inoDR paps);
tofpr 
hfoxp, S_Wpr    be6>b
chic unfikely(!(S_Wpblock numbxfs_b)v&&b(*
hfoxpu!sb_agcEXTNUM)bli_buf;
	*!C
		XF
	REALTIME et(ip(	ifv&&bE_FMT_DEV &&p->m_bmap_)_ = xfbp_ralerallag	fp->if_broo(EV &&PTAG_ount);
_ZERO(error"Acc
	xftorxfs_bmzero ;
	th */
%llu "error" numbkxfs_b:
%llx  numbkxnt:
%llx "error"blkcnt:
%llx 	xfs_i-e the:
%x 
hfox:
%x"(errors are equal in nodee to ri
{
	ce;rs are equal in nodeS_Wpblock numbxfs_b
	ce;rs are equal in nodeS_Wpblock numbxnt
	ce;rs are equal in nodeS_Wpblockxfs_bat);
,ufbrS_Wpblocke the, *
hfoxp);;
c*
hfoxpp) _agcEXTNUM;;
c*tofpuen			XFL be16_
		dmxr}
fL be16_dpnoright ordR be16sb typbloc-to aturn xfs_bmXFS_BM* Done oflip'iunuse 0;

	/(s) ord fihispblockec_t	l
 	}
	t1"len" logiurevct mbeigu entaddr0n"nderror.
Tlt n lid_t lowest-t */
	xfholep->t_iteblockhaxfholes,	ASSERne oflip'iaddr0tentshfor* btrnquntrbloc. ordR be16 0p->t_iteblockt nuent invctke spa(in-i
	fo) fil/
f_tur,


	xfs_file szl == 0)
	flip'_unuse (ate)
{ck set0;
tp,eeexte*k seaum de {
	int		level;		/* data r attetxtent length */
tree		*prk_len 0	_sta oextedize ntrholepentfi02ttree		*pfs_exntst_*flip'_unuse r or aunuse 0;

	/ tree_blocp, whichf6 or arrent btree block */
	xfk add_BMAP;	tto the extents list */
	xfs_blocidxmapp_dmwhichfo/*
	 *structu btree level, fo checke	xtent */
lock ct xfs_btreebp_rfs_exntst_
hfot */nts inlter xfs_bmXFS_BM*	eenbtreebp_rfs_exntst_
owestcke	xte
owest be1ful_buf_t		*bp;	/* s_exntst_maxmapp_dm numbile be1ful_buf_t		*bp;	/* s_exntst_xntmapp_dm	ip->ioes.
 *is_buf_t		*bp;	/*k);

		/*
 Note toff "bXFS_BM*ntrt			leobnoii/suct xf_read(s
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFOR_MAXEXel != 0]]]]
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFOR;
}

/*Xel != 0]]]]
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORk(mAcBROOBLOC
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORk(mAcB = xf*flip'_unuse _BTTREeh*
xfs_bmapf};
	/*
	 * Root level must use BMAP_BROO(mp, (i= NULLFflags &e				IF;
}

/*;bli_b]]]]p_BMAP_BTREE_i						xfs_inotp,
ust use BMAP_B|
	    be16_dex ino	
owest ne f = {_unuse ;oFnek_t	nexADDR macro ;
	A / (fl_f) -
	of(bp_release = 0)loFufs[i]dx)
		r 
hfot */)
		r maxp) 
owestcbrdx)< * Note toDDdxblo_recs = xelease = ho_all  {0p) {
		i Nock, i	xf(DR paidx)koc	/

STAbp_releasp, *key 256) ep);;
c		 */
		Seep->t_iteholepbefoexoopintA			leold hawAP_k, i, sz);
		eb.br>oi
owest +XFS_x&&bb.br- maxp> XFS_f = xf;*flip'_unuse _BTmaxm
		XL be16_	if (} le
hfot */)
	b.br+ bp_releasp, *xfs_bat);
 ep);;
cmaxp) 			bpILEOFF_MAX(
hfot */,i
owest6>bc}
L*flip'_unuse _BTmaxm
	*
xfs_bmapat will bR be16sb typbloc-to aturn xfs_bmXFS_BM* Done olter xfs_bm-		pbefoexll blter*xfs_ba(inputist */)d fihispblocror.
Tlt n lino= basxf	ST i__-
	t iCeis	basxf	ST ne owhichfo/*
	 *  filbR be16sb0oes.
ke spablocs,	as * by
doino=  avaiwhichfo/*
	 *  fil/
f_tur,


	xfs_file szl == 0)
	lter*xefoex(ate)
{ck set0;
tp,eeexte*k seaum de {
	int		level;		/* data r attetxtent length */
tree		*pfs_exntst_*lter*xfs_b,ts inlter xfs_bmtree_blocp, whichf6 or arrent btree block */
	xfbp_rfs_exntst__btcke	xtentputiblock	ip->iotree_blocexncke	xtehittrnquntrblockt			bp_release = ho_all  {0, nter to currentnter to nexttree_bloceBMAP;	tto the extents list */
	xfs		*peleasie = 0;S_W;	tto tlock" */to nextst */
	xfs_ifoevel, fo checke	xtent */
lock ct xfs_btreebp_rrk_t		*if
hfoxntss inlter to nextuse _	xfs		*peleasie = 0; lasckenter last entA			leost */
	xf;
	/ (
		XFS_IFORK_NEXTENTS(ip, whichfattroffset(ip) >> _MAXEXli_b]]]]
		XFS_IFORK_NEXTENTS(ip, whichfattroffset(ip) >> ;
}

/*Xli_b]]]]
		XFS_IFORK_NEXTENTS(ip, whichfattroffset(ip) >> k(mAcB != 0]]]]L be16_-xIOROOBLOC
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORk(mAcB = xf*lter*xfs_baBTTREeh*
xfs_bmapf};
	/*
	 * Root level must use BMAP_BROO(mp, (i= NULLFflags &e				IF;
}

/*;bli_b]]]]p_BMAP_BTREE_i						xfs_inotp,
ust use BMAP_B|
	    be16_dex ino	uXo
	  lter*xfs_ba-				X_bp) {
		 0)
		earet_	xfs_inoD paps);
p, whichf,k&tof, .
	 *x, .S_W,ufb&tent)kocBLOCKntr||Abp_releasp, *key 256) ep) >il leaT(ofBLOCtentUP_G_rec.b.br=(b_agcoILEOFF;
		X*lter*xfs_baBTTREehif (*thi*lter*xfs_baBTtentUP_G_rec.b.br+TtentUP_Gxfs_bat);
LEV}OOin the O_ADDwisot*lter*xfs_bais	ll				nt* bt_extnu sews_,
_recs;*
xfs_bmapat  xfzl == 0)
	lter*ck_t	n_xfs_fsblock_t*k se	
tp,	c
	uint			offset;

	i,ee_block	*block;	,	c
	uint			ofeleasie =	*e =,ee_block*in_empty/
	xfs_fsblock_tevel,o che
	 * Root level must use BMAP_BRO;	/* index inO;	/* in* Note to
OO(mp, (i= NULLFflags &e				IF;
}

/*;p = xf_BMAP_BTREE_i						xfs_inotp,
ust use BMAP_B;;
cBLOCK(bp);
		XL be16_dex ino	turnnek_t	nexADDR macro ;
	A /  -
	of(bp_release = 0)loFxic  Chet	nexA(inc = xf*in_emptyuen			XFL be16_0no	turnbp_releasgupdate({
		i Nock, i	xf(DR pa Chet	nex-			, e =	buf in_emptyuen0m
	*
xfs_bmapat will bad a pne olter nt */
whichfo abr	fs_mt(
	wheTIC iopint	de "od de ld ha/
	ult ord fiaddr0n"be{
		lde "ode	*aor* btrnquntrhispblocr WhS_xweb	de "oderk-w xfs_   ah "len"aor* btrnquntrhispbloc use B
doino= key 2uaor* bt last entrrentpfs_b
	  awckecevioint exrlign/
	in(iwah "len"aors_fiper (itt	  02aii/sur_ttentR be16sb1d fiama->aKntr->t_iteblock(whichfintAmptyuaser=y (iwawrifeuld hab, whert,t btshfor* btEOF,
	xfs_fileoff_tzl == 0)
	isaKnt(	c
	uint			ofel	de "o	*el	,ee_block	*block;	/
	xfs_fsblock_teleasie =	e =nO;	/* inin_emptyRO;	/* index inOo	uma->aKntr= TREe_BMAP_BTl == 0)
	lter*ck_t	n__buf_ouma->TS(ip, whichf,k&e =,ee			0]]]]}in_empty/kocBLOCK(bp);
		  be16_dex inof 	/ (,n_empty/ = xfuma->aKntr= 			XFL be16_0no	turnng the ed a pifxweb	ret	de "od de  btshfor* btlter to nex,t btre 	}
	t1in *ult_attrolter er */
	i	de "ode	*to nexum_recs;Tma->aKntr= Tma->	ip->io> Xe =UP_G_rec.b.br+Te =UP_Gxfs_bat);
}el !	(Tma->	ip->io> Xe =UP_G_rec.b.brli_bufis		wh_rec.xfs_b(e =UP_G_rec.pSERT)dm
	*
xfs_bmapat will bR be16sb typbloc-to aturn xfs_bmXFS_BM* Done oblip'ile, heshfortof1in
t_attroblocr 
Tlt n lino= basxf	ST i__-
	t iCeis	basxf	ST ne owhichfo/*
	 *  filbR be16sb0oes.
ke spablocs,	as * by
doino=  avaiwhichfo/*
	 *  fil/
f_tzl == 0)
	lter*	ip->i_	c
	uint			offset;

	i,eebp_rfs_exntst__*lter*xfs_b,ee_block	*block;	/
	xfs_fsblock_teleasie =	e =nO;	/* inin_emptyRO;	/* index inOo	*lter*xfs_baBTTREOOBLOC
		XFS_IFORK_NEXTENTS(ip, whichfork) >
			XFS_IFORk(mAcB
		  be16_to_cpu(bl
		XFS_IFORK_NEXTENTS(ip, whichfattroffset(ip) >> _MAXEXli_b]]]]
		XFS_IFORK_NEXTENTS(ip, whichfattroffset(ip) >> ;
}

/*B != 0]]]]L be16_-xIOROEe_BMAP_BTl == 0)
	lter*ck_t	n__buf_oTS(ip, whichf,k&e =,]}in_empty/kocBLOCK(bp)r||Ain_empty/
		  be16_dex inof *lter*xfs_baBTe =UP_G_rec.b.br+Te =UP_Gxfs_bat);
m
	*
xfs_bmapat will bR be16sbwheTIC iop, selectedves.k* Done ont */
haxfexaumvcte a   ah "le s.
notr 
Fs.
 */
xfs_inodkxwebcd a pneis	mrset	A di__-
	t ord mplyile (levellR'soB sge csb0..b_-
	-1 fil/
f_tur,

	xf1=>1= 1)
			0=>o_ADDwisot*szl == 0)
	 funxfs_b(vel;		/* data r attextent length */
tree_blocp, whichf6 r arrent btree block */
	xfbp_release = ho_all  {0, 0}; trrentlock's to nexttreebp_revel, fo checkextent */
lock ct xfs_btree_blocrst nts in nts list */
	xfs		*peleasie = 0;sckextentfs	nal ts_de dontrt			leo	xf;#ifnfs_btree_c
, ipDINODE_FMT_DEV &&p->m_bmap_
		  be16_
		XF
IZE_d.dT_DEfp->if_broovel is reah "len-
	no#chb(mexte!tree_btree_(bl
		XFS_IFO);
}

/*
 * Update the r!					XFL be16_0no	u(bl
		XFS_IFORK_NEXTENTS(ip, whichfattroffset(ip) >> ;
}

/*B !FL be16_0no	u(*
	 * Root level must use BMAP_BROO_read(si= NULLFflags &e				IF;
}

/*;

	ebp) {
		i Nock, i	xf(DR pa0BROO = xuneasgupdate(e pa&B);;
rst p) sUP_G_rec.b.br=(b0x&&bsUP_Gxfs_bat);
}== 			X, iprst p&& DINODE_FMT_DEV &&p->m_bmap_
	f_read(s
		XF
IZE_d.dT_DEfp->if_broovel is reah "len-
	dm
	*
xfs_brst npat will bEhichfo der e}
	podesff, ximum deptuse _dur{
		lde "od{de.mh of a bmape nts_e/aaer */
	i	de "od{de  */RKreal ede "od{de.mh of_fileoff_tur,
	xfs_fileoff_t		of#inadd/t			le_er */K			d(	c
	uint			ofel	de "o	*el	/
	xfs_fsblock_teleasie =	*(iwa= &Tma->g get ,_tur,orxf, 0};tempist */
	xfs		*pelease = ho_allf {0, _dmwhichfobnointf rbrdx), j;	/* index into the extents list */
	xfs	/* ini, 0};tempie theT btree level, for checkingnt */
lock ct xfs_btreebp_rfs_exntst_	newxchboxf, 0};rnquntp->iontr(iwabnoint sz)		*peleasie = 0;	r[3]of "bXeextbtnuwhichfobnoii/suct ur,

	xfver  csb0,t_extnucsb1r    bucsb2_leve!= XFSrst =0;s in nts list */
(logg{
		flags)	leve!= XFS_ theT_B0;_dm nuosspiev,macc
	x.
iouruevel);sbtreebp_rfs_s =  0;
da	new;  "bXew at);
}del 	de "ih "len"use _	xfs		*pfs_s =  0;
da	old;  "bold at);
}del 	de "ih "len"use _	xfs		*pfs_s =  0;
temp=0;s inst */
ks).xa	news recodesff,ex	xfs		*pfs_s =  0;
temp2=0;_dmst */
ks).xa	news recodesff,ex	xfs!= XFStmp_rst nt0}; umbial logg{
		flags rk)o	u(*
	 * Root level muma->TS(idiv(len, maxrecsn) < mp->mma->Tdxp> Xncey _read(smma->Tdxp<ADDR macro ;
	A /  -
	of(s_fsblock_telease =	cey _read(s is		wh_rec.xfs_b(newblock numbxfs_b)cey _read(s mma->FS_Fel != 0]]]]pmma->FS_ the private.b.flags &s = mgrCUR_BPRV_WASDEL)ecsn) = mSfilRo		C(xs
add/t	l;	/)t;
#fs_ine	LEFT;	r[0]
#fs_ine	RIGHT;	r[1]
#fs_ine	PREV;	r[2]urnng the Set upintpimuhontrvaiiablexftorm voickingests  -mple_,
_recs;ebp) {
		i Nock, i	xf(DR pamma->TdxBROO = xuneasgupdate(e pa&PREV);oFnewxchboxfY_Cetwblock numbers +Cetwblockxfs_bat);
m
	_read(sPREVUP_G_rec.b.br<_Cetwblock numbers)m
	_read(sPREVUP_G_rec.b.br+ PREVUP_Gxfs_bat);
}>_Cetwxchboxfecsn)da	oldp) snumbxfs_bst (PREVUP_G_rec.FS_WA<bufxa	newsBTTREOOng the Set flags r	fs_mt({
		w
 */ umb* Done o last entrr */
	i	de "od{de the to next n"be{
		replacfs_extRKreal ede "od{de.mXTNUM;
		sPREVUP_G_rec.b.br=_Cetwblock numbers)
FS_ theT|=6_to_cLEFTbpILLING;M;
		sPREVUP_G_rec.b.br+ PREVUP_Gxfs_bat);
}=_Cetwxchboxfe
FS_ theT|=6_to_cRIGHTbpILLING;Mrnng the ed a p = 0set flags ->t_i n"segm, i
haxfafver  Xeextbtn. the Don'tbset  mbeigu ent->t_itecombinrdowhichfowfs_bmap_tontntrgoXEXTNUM;
		smma->Tdxp>inc = xf_ theT|=6_to_cLEFTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pamma->Tdxp-			, &LEFTecsn) 	/ (,n		wh_rec.xfs_b(LEFTUP_G_rec.pSERT)d
	xf_ theT|=6_to_cLEFTbDELAY;dee 
eu(bl(_ theT&6_to_cLEFTbVALIDfv&&b!(_ theT&6_to_cLEFTbDELAY;bli_b]]]]LEFTUP_G_rec.b.br+ LEFTUP_Gxfs_bat);
}=_Cetwblock numbers li_b]]]]LEFTUP_G_rec.h "le + LEFTUP_Gxfs_bat);
}=_Cetwblock numbh "le li_b]]]]LEFTUP_G_reheT__Cetwblock nuheT&i_b]]]]LEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
r<_CMAX;
}LEN)
FS_ theT|=6_to_cLEFTbCONTIG;Mrnng the ed a p = 0set flags ->t_i n"segm, i
haxfaf_extnuXeextbtn. the Don'tbset  mbeigu ent->t_itecombinrdowhichfowfs_bmap_tontntrgoXEXTN Alsobcd a pxtnuall-ouree- mbeigu entae{
		tontntrgoXEXTNUM;
		smma->Tdxp<ouma->TSckingf.LLFS;
	Ax/ (fl_f) -
	of(bp_release = 0)p-			 = xf_ theT|=6_to_cRIGHTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pamma->Tdxp+			, &RIGHTecsn) 	/ (,n		wh_rec.xfs_b(RIGHTUP_G_rec.pSERT)d
	xf_ theT|=6_to_cRIGHTbDELAY;dee 
eu(bl(_ theT&6_to_cRIGHTbVALIDfv&&b!(_ theT&6_to_cRIGHTbDELAY;bli_b]]]]newxchboxfY_= RIGHTUP_G_rec.ers li_b]]]]etwblock numbh "le +Cetwblockxfs_bat);
r_= RIGHTUP_G_rec.h "le li_b]]]]etwblock nuheT_= RIGHTUP_G_reheT&i_b]]]]etwblockxfs_bat);
r+ RIGHTUP_Gxfs_bat);
r<_CMAX;
}LENbli_b]]]]p(_ theT&6(_to_cLEFTbCONTIG |6_to_cLEFTbpILLING |_buf;
	* 6_to_cRIGHTbpILLING)hfat_buf;
	* (_to_cLEFTbCONTIG |6_to_cLEFTbpILLING |_buf;
	* 6_to_cRIGHTbpILLING)Fel != 0]]LEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
r+ RIGHTUP_Gxfs_bat);

	xf<_CMAX;
}LEN)e
FS_ theT|=6_to_cRIGHTbCONTIG;Mrn_BMAP_BTTREeng the Sreset el *basxf	ST ne opILLING  = 0CONTIG  nuosspievXEXTNUM; reset (_ theT&6(_to_cLEFTbpILLING |6_to_cLEFTbCONTIG |_bu	6_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG)e = x dela_to_cLEFTbpILLING |6_to_cLEFTbCONTIG |_b
	* 6_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG:;
c		 */
		Fthiions lprevi_FORK last enlyaer */
	i	de "od{de to nexum_	r.
Tl over   = 0_extnuXeextbtn bmp, both  mbeigu entec_t	etwk, i, sz);mma->Tdx--;s;n
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pamma->TdxB,ee		LEFTUP_Gxfs_bat);
}+CPREVUP_Gxfs_bat);
}+ee		RIGHTUP_Gxfs_bat);
);s;n
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy O = xi Nocremovreuma->TS(imma->Tdxp+		, 2, e theXLEVANma->TScking.y di_anexte--;s;n
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, RIGHTUP_G_rec.ers,error;RIGHTUP_G_rec.h "le,error;RIGHTUP_Gxfs_bat);
, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rip,		deleireuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rip,		decremt	n_uma->FS_, 0, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, LEFTUP_G_rec.b.b,error;LEFTUP_G_rec.h "le,error;LEFTUP_Gxfs_bat);
}+ee				PREVUP_Gxfs_bat);
}+ee		r;RIGHTUP_Gxfs_bat);
, LEFTUP_G_rehe_bytl	BLOCK(bp);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cRIGHTbpILLING |6_to_cLEFTbCONTIG:;
c		 */
		Fthiions lprevi_FORK last enlyaer */
	i	de "od{de to nexum_	r.
Tl over  Xeextbtnut numbeigu en,t* bt_extnu lino=k, i, sz);mma->Tdx--;ss;n
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pamma->TdxB,ee		LEFTUP_Gxfs_bat);
}+CPREVUP_Gxfs_bat);
);s;n
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy O = xi Nocremovreuma->TS(imma->Tdxp+		, 1, e theXLEVA
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, LEFTUP_G_rec.b.b,error;LEFTUP_G_rec.h "le,]LEFTUP_Gxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, LEFTUP_G_rec.b.b,error;LEFTUP_G_rec.h "le,error;LEFTUP_Gxfs_bat);
}+ee				PREVUP_Gxfs_bat);
, LEFTUP_G_rehe_bytl	BLOCK(bp);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG:;
c		 */
		Fthiions lprevi_FORK last enlyaer */
	i	de "od{de to nexum_	r.
Tl o_extnuXeextbtnut numbeigu en,t* btver  csbno=k, i, sz);
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_upd_rec.xfs_b(e pa Cwblock numbxfs_b)ey O = xuneas_updxfs_bat);
 e (erroPREVUP_Gxfs_bat);
}+ RIGHTUP_Gxfs_bat);
);s;n
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy O = xi Nocremovreuma->TS(imma->Tdxp+		, 1, e theXLEVA
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, RIGHTUP_G_rec.ers,error;RIGHTUP_G_rec.h "le,error;RIGHTUP_Gxfs_bat);
, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, PREVUP_G_rec.b.b,error; Cwblock numbxfs_b,ee				PREVUP_Gxfs_bat);
}+ee		r;RIGHTUP_Gxfs_bat);
, PREVUP_G_rehe_bytl	BLOCK(bp);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cRIGHTbpILLING:;
c		 */
		Fthiions lprevi_FORK last enlyaer */
	i	de "od{de to nexum_	r.
NeiTIC iop, ver  XexteextnuXeextbtn bmp,  mbeigu entec_tm_	r.

	in(iwae ak, i, sz);
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_upd_rec.xfs_b(e pa Cwblock numbxfs_b)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy ONma->TScking.y di_anexte++;s;n
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		Xmma->FS_ the e =UPUP_G_reheT_eturnEXT_NORMm
		X_BMAP_BTREE_Rip,		inserteuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} leing key x dela_to_cLEFTbpILLING |6_to_cLEFTbCONTIG:;
c		 */
		Fthiions lpne oblip'i umb* Doao last entrr */
	i	de "od{deum_	r.
Tl over  Xeextbtnut numbeigu enk, i, sz);
#inccl;		unt sprel indireuma->TS(imma->Tdxp-		, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pamma->Tdxp-			,ee		LEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
cey O = xuneas_upd_rec.56) ep(erroPREVUP_G numbers +Cetwblockxfs_bat);
)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdxp-		, e the, _THI		IP_cey
;
temp_BTPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
m
	n
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e patempXLEVA
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, LEFTUP_G_rec.b.b,error;LEFTUP_G_rec.h "le,]LEFTUP_Gxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, LEFTUP_G_rec.b.b,error;LEFTUP_G_rec.h "le,error;LEFTUP_Gxfs_bat);
}+ee				etwblockxfs_bat);
,error;LEFTUP_G_rehe_bytl	BLOCK(bp);
		X	S_WANde andel} lexa	newsBT			bpILBLKS_MIN(l;		unt swoip'_isdleneuma->TS(itempX,errosnumbxfs_bst (PREVUP_G_rec.FS_WA<cey O = xuneas_upd_rec.xfs_b(e pa 	wh_rec.xfs_b(xa	new))ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy ONma->Tdx--;s;ning key x dela_to_cLEFTbpILLING:;
c		 */
		Fthiions lpne oblip'i umb* Doao last entrr */
	i	de "od{deum_	r.
Tl over  Xeextbtnut nno= umbeigu enk, i, sz);
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_upd_rec.56) ep(Cetwxchboxfecs;
temp_BTPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
m
	n = xuneas_updxfs_bat);
 e patempXLEVA{
		i Nocinserteuma->TS(imma->Tdx, 1(Cetw, e theXLEVANma->TScking.y di_anexte++;s;n
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		Xmma->FS_ the e =UPUP_G_reheT_eturnEXT_NORMm
		X_BMAP_BTREE_Rip,		inserteuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} s;n
		sl;		unt sl xfsbbt
 *(uma->TS(idiv(len, maxref = xf;_BMAP_BTREE_Rf#inree leak->bbt
 *(uma->tp_ouma->TS(error;uma->t = {pSERT( uma->tv;	/aty cr;&uma->FS_, 	ns&tmp_rst t div(len, maxrecs)O;rst 
|= tmp_rst nytl	BLOCK(bp);
		X	S_WANde andel} lexa	newsBT			bpILBLKS_MIN(l;		unt swoip'_isdleneuma->TS(itempX,errosnumbxfs_bst (PREVUP_G_rec.FS_WA< -errosmma->FS_F? mma->FS_ the private.b.	de "ode	*: 0))ey Oebp) {
		i Nock, i	xf(DR pamma->Tdxp+			ey O = xuneas_upd_rec.xfs_b(e pa 	wh_rec.xfs_b(xa	new))ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdxp+		, e the, _THI		IP_cey Oing key x dela_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG:;
c		 */
		Fthiions lpttrolter  umb* Doao last entrr */
	i	de "od{deum_	r.
Tl o_extnuXeextbtnut numbeigu entec_t	
	in(iwa	de "od{deum_	r./
;
temp_BTPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
m
	n
#inccl;		unt sprel indireuma->TS(imma->Tdxp+		, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e patempXLEVA{
		uneas_upd	def({
		i Nock, i	xf(DR pamma->Tdxp+			,
or; Cwblock numbers,a Cwblock numbxfs_b,
or; Cwblockxfs_bat);
}+ RIGHTUP_Gxfs_bat);
,
or;RIGHTUP_G_rehe)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdxp+		, e the, _THI		IP_cey O
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, RIGHTUP_G_rec.ers,error;RIGHTUP_G_rec.h "le,error;RIGHTUP_Gxfs_bat);
, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, etwblock numbers,error; Cwblock numbxfs_b,ee				etwblockxfs_bat);
}+ee		r;RIGHTUP_Gxfs_bat);
,error;RIGHTUP_G_rehe_bytl	BLOCK(bp);
		X	S_WANde andel}  lexa	newsBT			bpILBLKS_MIN(l;		unt swoip'_isdleneuma->TS(itempX,errosnumbxfs_bst (PREVUP_G_rec.FS_WA<cey O
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_upd_rec.xfs_b(e pa 	wh_rec.xfs_b(xa	new))ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy ONma->Tdx++;s;ning key x dela_to_cRIGHTbpILLING:;
c		 */
		Fthiions lpttrolter  umb* Doao last entrr */
	i	de "od{deum_	r.
Tl o_extnuXeextbtnut nno= umbeigu enk, i, sz);
emp_BTPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
m
	n
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e patempXLEVA{
		i Nocinserteuma->TS(imma->Tdxp+		, 1, etw, e theXLEVANma->TScking.y di_anexte++;s;n
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		Xmma->FS_ the e =UPUP_G_reheT_eturnEXT_NORMm
		X_BMAP_BTREE_Rip,		inserteuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} s;n
		sl;		unt sl xfsbbt
 *(uma->TS(idiv(len, maxref = xf;_BMAP_BTREE_Rf#inree leak->bbt
 *(uma->tp_ouma->TS(erroruma->t = {pSERT( uma->tv;	/a &uma->FS_, 	nerror&tmp_rst t div(len, maxrecs)O;rst 
|= tmp_rst nytl	BLOCK(bp);
		X	S_WANde andel} lexa	newsBT			bpILBLKS_MIN(l;		unt swoip'_isdleneuma->TS(itempX,errosnumbxfs_bst (PREVUP_G_rec.FS_WA< -errosmma->FS_F? mma->FS_ the private.b.	de "ode	*: 0))ey Oebp) {
		i Nock, i	xf(DR pamma->TdxBROOO = xuneas_upd_rec.xfs_b(e pa 	wh_rec.xfs_b(xa	new))ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_ceyy ONma->Tdx++;s;ning key x dela0:;
c		 */
		Fthiions lpttromiddle  umb* Doao last entrr */
	i	de "od{deum_	r.
Cmbeiguityut nimpo_sibockhereum_	r.
Tlt nudelaint	void
	i	dall lrevickingimeum_	r.m_	r.
We key 2uec_t	ltrr */
	i	de "od{de:m_	r.m_	r.
+ddddddddddddddddddddddddddddddddddddddddddddddddddddddd+m_	r.
TPREV @brdxm_	r.m_         *  = 0web	ret	de "od ng:m_	r.
TTTTTTTTTTTTTTTTTTTT+rrrrrrrrrrrrrrrrr+m_	r.	buf;
	* newm_	r.m_	r.
 = 0webset it upif rbrnsert{de as:m_	r.
+ddddddddddddddddddd+rrrrrrrrrrrrrrrrr+ddddddddddddddddd+m_	r.
TTTTTTTTTTTTTTTTTTTTTTTTTTTnewm_	r.
TPREV @brdxTTTTTTTTTTLEFTTTTTTTTTTTTTTTRIGHTm_	r.
TTTTTTTTTTTTTTTTTTTTTrnserte	*aorTdxp+		, i, sz);
emp_BTetwblock numbers - PREVUP_G_rec.b.bcs;
temp2_BTPREVUP_G_rec.b.br+ PREVUP_Gxfs_bat);
}-Cetwxchboxfm
	n
#inccl;		unt sprel indireuma->TS(imma->Tdx, 0, _THI		IP_cey O = xuneas_updxfs_bat);
 e patempXLexte*kun"oderPREV  sz);LEFTT= *(iwey ORIGHTUP_G_reheT= PREVUP_G_reheey ORIGHTUP_G_rembxfs_bmap 	wh_rec.xfs_b(
		X	(l_f)l;		unt swoip'_isdleneuma->TS(itemp2))ey ORIGHTUP_G_rec.ers =Cetwxchboxfm
	nRIGHTUP_Gxfs_bat);
r=itemp2;
kextentsertTLEFTT(r[0])
 = 0RIGHTT(r[1])uaor* btsamingime  sz);{
		i Nocinserteuma->TS(imma->Tdxp+		, 2, &LEFT, e theXLEVANma->TScking.y di_anexte++;s;n
		smma->FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		Xmma->FS_ the e =UPUP_G_reheT_eturnEXT_NORMm
		X_BMAP_BTREE_Rip,		inserteuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} s;n
		sl;		unt sl xfsbbt
 *(uma->TS(idiv(len, maxref = xf;_BMAP_BTREE_Rf#inree leak->bbt
 *(uma->tp_ouma->TS(error;uma->t = {pSERT( uma->tv;	/a &uma->FS_,error;	ns&tmp_rst t div(len, maxrecs)O;rst 
|= tmp_rst nytl	BLOCK(bp);
		X	S_WANde andel} le
emp_BTl;		unt swoip'_isdleneuma->TS(itempXcs;
temp2_BTl;		unt swoip'_isdleneuma->TS(itemp2)cs;
orxf =att_f)(
emp_+itemp2 -erro;
	* (snumbxfs_bst (PREVUP_G_rec.FS_WA< -errof;
	* (mma->FS_F?errof;
	*  mma->FS_ the private.b.	de "ode	*: 0))cey O
		sorxf 		bp = xf;_BMAP_BTREE_icsbep;dify_at);
ers(Nma->TSckin_broo(error) = mSB	bpDnt);
S(error)-((intCK(m)orxf)pa0BROO	f_read(s!K(bp);bytl	BLOCK(bp);
		X	S_WANde andel}  leebp) {
		i Nock, i	xf(DR pamma->TdxBROOO = xuneas_upd_rec.xfs_b(e pa 	wh_rec.xfs_b(tt_f)tempX)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_cey	n
#inccl;		unt sprel indireuma->TS(imma->Tdxp+	2, e the, _THI		IP_cey O = xuneas_upd_rec.xfs_b({
		i Nock, i	xf(DR pamma->Tdxp+	2	,
or; 	wh_rec.xfs_b(tt_f)temp2))ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdxp+	2, e the, _THI		IP_ceyy ONma->Tdx++;s;nxa	newsBT
emp_+itemp2;s;ning key x dela_to_cLEFTbpILLING |6_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
 dela_to_cRIGHTbpILLING |6_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
 dela_to_cLEFTbpILLING |6_to_cRIGHTbCONTIG:;
 dela_to_cRIGHTbpILLING |6_to_cLEFTbCONTIG:;
 dela_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
 dela_to_cLEFTbCONTIG:;
 dela_to_cRIGHTbCONTIG:;
c		 */
		Theela delsb	ret	denimpo_sibock, i, sz);_read(s06>bc}
bm cuc nts_e/ */RKoid
xei*/
ec
	xaint sz)
		sl;		unt sl xfsbbt
 *(uma->TS(idiv(len, maxref = xf!= Xtmp_logflagsnt0}; umbial log flagn nts list  rk)o	 _read(smma->FS_F=sb_agcBREehiBMAP_BTREE_Rf#inree leak->bbt
 *(uma->tp_ouma->TS(erroruma->t = {pSERT( uma->tv;	/a &uma->FS_,errorda	oldp> 0, &tmp_logflagst div(len, maxrecs)Ouma->logflags
|= tmp_logflagsn
l	BLOCK(bp);
		XS_WANde ande}
bm cuadjl = cture  sgek ;
	reelrve _dr */
	iisdie =tih "len" sz)
		sda	oldp||.xa	newf = xf
emp_BTda	new;s;n
		smma->FS_;
		X
emp_+= mma->FS_ the private.b.	de "ode	;s;n
		s
emp_< da	old	reasREE_icsbep;dify_at);
ers(Nma->TSckin_broo(error) = mSB	bpDnt);
S(error)(intCK(m)sda	oldp-itempX, 06>bc}
bm cucleaevel r* bt	de "ode	*lormansde auec_t	i  Xews lprnya del.TNUM;
		smma->FS_;
		mma->FS_ the private.b.	de "ode	*BTTREOOREE_Rf#inor nul_u.l		xfs_inouma->FS_, uma->TS(idiv(len, maxre;
de a:
Ouma->logflags
|= rst nyt  be16_dex ino#unfs_;LEFTo#unfs_;RIGHTm#unfs_;PREVpat will ba nts_e/afiunwrifdeni	de "od{de  */RKreal ede "od{det btvice ts_da.mh of_fileoff_tur,
	xfs_fileoff_t		of#inadd/t			le_unwrifdenK			d(	c
	uint			of*k se	
tp,	cl;		/* data or attxtent length */
ct xfs_btreebp_rrk_t		*ifor dx, _dmwhichfoXFS_BM* */ indir/ntsertT sz)		*peip,		FS_*ifor*FS_attxtenf *FS_aut nn	wh,nno= RKoid
xe sz)		*peleasie = 0;	*(iw,	 "bXew rrent */Rddpentfilint			leve	xfs		*pfbuffer for*flip',nter to currentf = {pSERTrvaiiablee sz)		*pel)
	fp,		for*fv;	/a
	xfv;	/*ntrt			lexftorxe"nderdx	xfs!= XFS*logflagsp) ingnt */
logg{
		flags rk){z)		*peip,		FS_*iforFS_ system mouFS_so_btreebp_release = ho_allf {0, _dmwhichfobnointf rbrdx), j;	/* index into the extents list */
	xfs	/* ini, 0};tempie theT btree level, for checkingnt */
lock ct xfs_btreebp_rfs_exntst_	newxchboxf, 0};rnquntp->iontr(iwabnoint sz)		*prknt_allf	newwhi;	 "bXew whichfoe theT btree lrknt_allf	oldehi;	 "boldpwhichfoe theT btree leleasie = 0;	r[3]of "bXeextbtnuwhichfobnoii/suct ur,

	xfver  csb0,t_extnucsb1r    bucsb2_leve!= XFSrst =0;s in nts list */
(logg{
		flags)	leve!= XFS_ theT_B0;_dm nuosspiev,macc
	x.
iouruevel);sbtre
S*logflagsp*BTTREOOFS_F= *FS_a;o	u(*
	 * Root level must div(len, maxrecsn) < mp->*Tdxp> Xncey _read(s*Tdxp<ADDR macro ;
	A /  -
	of(s_fsblock_telease =	cey _read(s is		wh_rec.xfs_b(newblock numbxfs_b)ceyn) = mSfilRo		C(xs
add/t	l;	/)t;
#fs_ine	LEFT;	r[0]
#fs_ine	RIGHT;	r[1]
#fs_ine	PREV;	r[2]urnng the Set upintpimuhontrvaiiablexftorm voickingests  -mple_,
_recs;eBMAP_BTTREeebp) {
		i Nock, i	xf(DR pa*TdxBROO = xuneasgupdate(e pa&PREV);oFnewip, _Cetwblock nuhe;oFoldehi =atnewip, __eturnEXT_UNWRITTEN)F?errturnEXT_NORM*: turnEXT_UNWRITTEN;
	_read(sPREVUP_G_reheT_= oldehi);oFnewxchboxfY_Cetwblock numbers +Cetwblockxfs_bat);
m
	_read(sPREVUP_G_rec.b.br<_Cetwblock numbers)m
	_read(sPREVUP_G_rec.b.br+ PREVUP_Gxfs_bat);
}>_Cetwxchboxfecsn)ng the Set flags r	fs_mt({
		w
 */ umb* Done o last entoldehi 	de "od{de the to next n"be{
		replacfs_extRKnewip, ede "od{de.mXTNUM;
		sPREVUP_G_rec.b.br=_Cetwblock numbers)
FS_ theT|=6_to_cLEFTbpILLING;M;
		sPREVUP_G_rec.b.br+ PREVUP_Gxfs_bat);
}=_Cetwxchboxfe
FS_ theT|=6_to_cRIGHTbpILLING;Mrnng the ed a p = 0set flags ->t_i n"segm, i
haxfafver  Xeextbtn. the Don'tbset  mbeigu ent->t_itecombinrdowhichfowfs_bmap_tontntrgoXEXTNUM;
		s*Tdxp>inc = xf_ theT|=6_to_cLEFTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pa*Tdxp-			, &LEFTecsn) 	/ (,n		wh_rec.xfs_b(LEFTUP_G_rec.pSERT)d
	xf_ theT|=6_to_cLEFTbDELAY;dee 
eu(bl(_ theT&6_to_cLEFTbVALIDfv&&b!(_ theT&6_to_cLEFTbDELAY;bli_b]]]]LEFTUP_G_rec.b.br+ LEFTUP_Gxfs_bat);
}=_Cetwblock numbers li_b]]]]LEFTUP_G_rec.h "le + LEFTUP_Gxfs_bat);
}=_Cetwblock numbh "le li_b]]]]LEFTUP_G_reheT__Cetwip, &i_b]]]]LEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
r<_CMAX;
}LEN)
FS_ theT|=6_to_cLEFTbCONTIG;Mrnng the ed a p = 0set flags ->t_i n"segm, i
haxfaf_extnuXeextbtn. the Don'tbset  mbeigu ent->t_itecombinrdowhichfowfs_bmap_tontntrgoXEXTN Alsobcd a pxtnuall-ouree- mbeigu entae{
		tontntrgoXEXTNUM;
		s*Tdxp< TSckingf.LLFS;
	Ax/ (fl_f) -
	of(bp_release = 0)p-			 = xf_ theT|=6_to_cRIGHTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pa*Tdxp+			, &RIGHTecs) 	/ (,n		wh_rec.xfs_b(RIGHTUP_G_rec.pSERT)d
	xf_ theT|=6_to_cRIGHTbDELAY;dee 
eu(bl(_ theT&6_to_cRIGHTbVALIDfv&&b!(_ theT&6_to_cRIGHTbDELAY;bli_b]]]]newxchboxfY_= RIGHTUP_G_rec.ers li_b]]]]etwblock numbh "le +Cetwblockxfs_bat);
r_= RIGHTUP_G_rec.h "le li_b]]]]etwip, __eRIGHTUP_G_reheT&i_b]]]]etwblockxfs_bat);
r+ RIGHTUP_Gxfs_bat);
r<_CMAX;
}LENbli_b]]]]p(_ theT&6(_to_cLEFTbCONTIG |6_to_cLEFTbpILLING |_buf;
	* 6_to_cRIGHTbpILLING)hfat_buf;
	* (_to_cLEFTbCONTIG |6_to_cLEFTbpILLING |_buf;
	* 6_to_cRIGHTbpILLING)Fel != 0]]LEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
r+ RIGHTUP_Gxfs_bat);

	xf<_CMAX;
}LEN)e
FS_ theT|=6_to_cRIGHTbCONTIG;Mrnng the Sreset el *basxf	ST ne opILLING  = 0CONTIG  nuosspievXEXTNUM; reset (_ theT&6(_to_cLEFTbpILLING |6_to_cLEFTbCONTIG |_bu	6_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG)e = x dela_to_cLEFTbpILLING |6_to_cLEFTbCONTIG |_b
	* 6_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG:;
c		 */
		Sett{
		lde* Doao last entoldehi whichfo abetwip,um_	r.
Tl over   = 0_extnuXeextbtn bmp, both  mbeigu entec_t	etwk, i, sz);--*Tdx;ss;n
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pa*TdxB,ee		LEFTUP_Gxfs_bat);
}+CPREVUP_Gxfs_bat);
}+ee		RIGHTUP_Gxfs_bat);
);s;n
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_cey
 O = xi Nocremovrei pa*Tdxp+		, 2, e theXLEVATScking.y di_anexte -= 2cs) 	/ (FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, RIGHTUP_G_rec.ers,error;RIGHTUP_G_rec.h "le,error;RIGHTUP_Gxfs_bat);
, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		deleireFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		decremt	n_FS_, 0, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		deleireFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		decremt	n_FS_, 0, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_, LEFTUP_G_rec.b.b,errorLEFTUP_G_rec.h "le,errorLEFTUP_Gxfs_bat);
}+CPREVUP_Gxfs_bat);
}+ee		;RIGHTUP_Gxfs_bat);
, LEFTUP_G_rehe_);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cRIGHTbpILLING |6_to_cLEFTbCONTIG:;
c		 */
		Sett{
		lde* Doao last entoldehi whichfo abetwip,um_	r.
Tl over  Xeextbtnut numbeigu en,t* bt_extnu lino=k, i, sz);--*Tdx;ss;n
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pa*TdxB,ee		LEFTUP_Gxfs_bat);
}+CPREVUP_Gxfs_bat);
);s;n
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_cey
 O = xi Nocremovrei pa*Tdxp+		, 1, e theXLEVATScking.y di_anexte--;s;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, PREVUP_G_rec.b.b,error;PREVUP_G_rec.FS_WA,CPREVUP_Gxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		deleireFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		decremt	n_FS_, 0, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_, LEFTUP_G_rec.b.b,errorLEFTUP_G_rec.h "le,errorLEFTUP_Gxfs_bat);
}+CPREVUP_Gxfs_bat);
,errorLEFTUP_G_rehe_);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG:;
c		 */
		Sett{
		lde* Doao last entoldehi whichfo abetwip,um_	r.
Tl o_extnuXeextbtnut numbeigu en,t* btver  csbno=k, i, sz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e (erroPREVUP_Gxfs_bat);
}+ RIGHTUP_Gxfs_bat);
);s;n = xuneas_upd_reireep(Cetwehi);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_cey O = xi Nocremovrei pa*Tdxp+		, 1, e theXLEVATScking.y di_anexte--;s;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, RIGHTUP_G_rec.ers,error;RIGHTUP_G_rec.h "le,error;RIGHTUP_Gxfs_bat);
, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		deleireFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rip,		decremt	n_FS_, 0, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_, etwblock numbers,error Cwblock numbxfs_b,ee			 Cwblockxfs_bat);
}+ RIGHTUP_Gxfs_bat);
,
or;Fnewip,_);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cRIGHTbpILLING:;
c		 */
		Sett{
		lde* Doao last entoldehi whichfo abetwip,um_	r.
NeiTIC iop, ver  XexteextnuXeextbtn bmp,  mbeigu entec_tm_	r.

	in(iwae ak, i, sz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_upd_reireep(Cetwehi);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceys;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_, etwblock numbers,error Cwblock numbxfs_b,Cetwblockxfs_bat);
,errornewip,_);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING |6_to_cLEFTbCONTIG:;
c		 */
		Sett{
		ne oblip'i umb* Doao last entoldehi whichfo abetwip,um_	r.
Tl over  Xeextbtnut numbeigu enk, i, sz);
#inccl;		unt sprel indirei pa*Tdxp-		, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pa*Tdxp-			,ee		LEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
cey O = xuneas_upd_rec.56) ep(erroPREVUP_G numbers +Cetwblockxfs_bat);
)ey O
#inccl;		unt spo_al indirei pa*Tdxp-		, e the, _THI		IP_ceyz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_upd_rec.xfs_b(e p
ror Cwblock numbxfs_b}+Cetwblockxfs_bat);
cey O = xuneas_updxfs_bat);
 e (erroPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceys;n--*Tdx;ss;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, PREVUP_G_rec.b.b,error;PREVUP_G_rec.FS_WA,CPREVUP_Gxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_,
ror;PREVUP_G_rec.ers +Cetwblockxfs_bat);
,
ror;PREVUP_G_rec.xfs_b}+Cetwblockxfs_bat);
,
ror;PREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
,
ror;oldehi));
		X	S_WANde andeleu(bl(_BMAP_BTREE_Rip,		decremt	n_FS_, 0, &i_);
		X	S_WANde andele_BMAP_BTREE_Rff,
 indireFS_, LEFTUP_G_rec.b.b,errorLEFTUP_G_rec.h "le,errorLEFTUP_Gxfs_bat);
}+Cetwblockxfs_bat);
,
ror;LEFTUP_G_rehe_bytl	BLOCK(bp);
		X	S_WANde andel} leing key x dela_to_cLEFTbpILLING:;
c		 */
		Sett{
		ne oblip'i umb* Doao last entoldehi whichfo abetwip,um_	r.
Tl over  Xeextbtnut nno= umbeigu enk, i, sz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O_read(sepv&&bbp_releasgupd_reireep)T_= oldehi);oFO = xuneas_upd_rec.56) ep(Cetwxchboxfecs;
 = xuneas_updxfs_bat);
 e (erroPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
);oFn = xuneas_upd_rec.xfs_b(e p
ror Cwblock numbxfs_b}+Cetwblockxfs_bat);
cey O
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_cey
 O = xi Nocinsertei pa*Tdx, 1, etw, e theXLEVATScking.y di_anexte++;s;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, PREVUP_G_rec.b.b,error;PREVUP_G_rec.FS_WA,CPREVUP_Gxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_,
ror;PREVUP_G_rec.ers +Cetwblockxfs_bat);
,
ror;PREVUP_G_rec.xfs_b}+Cetwblockxfs_bat);
,
ror;PREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
,
ror;oldehi));
		X	S_WANde andeleFS_ the e =UPT= *(iwey Oeu(bl(_BMAP_BTREE_Rip,		inserteFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} leing key x dela_to_cRIGHTbpILLING |6_to_cRIGHTbCONTIG:;
c		 */
		Sett{
		ttrolter  umb* Doao last entoldehi whichfo abetwip,um_	r.
Tl o_extnuXeextbtnut numbeigu entec_t	
	in(iwa	de "od{deum_	r./
;
t#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e (erroPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceys;n++*Tdx;ss;n
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_upd	def({
		i Nock, i	xf(DR pa*TdxB,ee		 Cwblock numbers,a Cwblock numbxfs_b,
or; Cwblockxfs_bat);
}+ RIGHTUP_Gxfs_bat);
,Cetwehi);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceys;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG D;
}REehif ( = xf;rst 
	 0m
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, PREVUP_G_rec.b.b,error;PREVUP_G_rec.FS_WA,error;PREVUP_Gxfs_bat);
, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_, PREVUP_G_rec.b.b,errorPREVUP_G_rec.FS_WA,errorPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
,
ror;oldehi));
		X	S_WANde andeleu(bl(_BMAP_BTREE_Rip,		incremt	n_FS_, 0, &i_);
		X	S_WANde andeleu(bl(_BMAP_BTREE_Rff,
 indireFS_, etwblock numbers,error Cwblock numbxfs_b,ee			 Cwblockxfs_bat);
}+ RIGHTUP_Gxfs_bat);
,
or;Fnewip,_);
		X	S_WANde andel} leing key x dela_to_cRIGHTbpILLING:;
c		 */
		Sett{
		ttrolter  umb* Doao last entoldehi whichfo abetwip,um_	r.
Tl o_extnuXeextbtnut nno= umbeigu enk, i, sz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e (erroPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceys;n++*Tdx;s O = xi Nocinsertei pa*Tdx, 1, etw, e theXLEEVATScking.y di_anexte++;s;n
		sFS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, PREVUP_G_rec.b.b,error;PREVUP_G_rec.FS_WA,CPREVUP_Gxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		Xu(bl(_BMAP_BTREE_Rff,
 indireFS_, PREVUP_G_rec.b.b,errorPREVUP_G_rec.FS_WA,errorPREVUP_Gxfs_bat);
}-Cetwblockxfs_bat);
,
ror;oldehi));
		X	S_WANde andeleu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		XFS_ the e =UPUP_G_reheT_eturnEXT_NORMm
		Xu(bl(_BMAP_BTREE_Rip,		inserteFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} leing key x dela0:;
c		 */
		Sett{
		ttromiddle  umb* Doao last entoldehi whichfo a */
		etwip,u 
Cmbeiguityut nimpo_sibockhereum_	r.
Onint			le becomexftureert			lexk, i, sz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 e (erroetwblock numbers - PREVUP_G_rec.b.b);oFn
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceys;nr[0]T= *(iwey Or[1]UP_G_rec.ers =Cetwxchboxfm
	nr[1]UP_Gxfs_bat);
r_
rorPREVUP_G_rec.b.br+ PREVUP_Gxfs_bat);
}-Cetwxchboxfm
	nr[1]UP_G_rec.xfs_b}_Cetwblock numbh "le +Cetwblockxfs_bat);
m
	nr[1]UP_G_reheT_eoldehi;ys;n++*Tdx;s O = xi Nocinsertei pa*Tdx, 2, &r[0], e theXLEEVATScking.y di_anexte_+= 2cs) 	/ (FS_F=sb_agcB
XFSrst 
	 * RooLOG (bpE |					ILOG D;
}REehif ( = xf;rst 
	 * RooLOG (bpEm
		Xu(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, PREVUP_G_rec.b.b,error;PREVUP_G_rec.FS_WA,CPREVUP_Gxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X "bXew _extnut			le -toldehi ct ur,u(bl(_BMAP_BTREE_Rff,
 indireFS_, r[1]UP_G_rec.ers,errorr[1]UP_G_rec.xfs_b, r[1]UP_Gxfs_bat);
,errorr[1]UP_G_rehe_);
		X	S_WANde andele "bXew ver  t			le -toldehi ct ur,FS_ the e =UPT= PREVm
		XFS_ the e =UPUP_Gxfs_bat);
r_
roroetwblock numbers - PREVUP_G_rec.b.bm
		Xu(bl(_BMAP_BTREE_Rip,		inserteFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		c		 */	r.
Reset _itecS_so_bWANne o osid{det f	
	in(iwat			le */	r.
web	ret	bel r*oentsertTas
webcan'tbtrl = itTafter */	r.
ne o last entntsert. */	r.t ur,u(bl(_BMAP_BTREE_Rff,
	xfs_fseqsFS_, etwblock numbers,error; Cwblock numbxfs_b,Cetwblockxfs_bat);
,error;&i_);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		X "bXew middle t			le -tetwip, ct ur,FS_ the e =UPUP_G_reheT_eetwblock nuhe;oFOeu(bl(_BMAP_BTREE_Rip,		inserteFS_, &i_);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} leing key x dela_to_cLEFTbpILLING |6_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
 dela_to_cRIGHTbpILLING |6_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
 dela_to_cLEFTbpILLING |6_to_cRIGHTbCONTIG:;
 dela_to_cRIGHTbpILLING |6_to_cLEFTbCONTIG:;
 dela_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
 dela_to_cLEFTbCONTIG:;
 dela_to_cRIGHTbCONTIG:;
c		 */
		Theela delsb	ret	denimpo_sibock, i, sz);_read(s06>bc}
bm cuc nts_e/ */RKoid
xei*/
ec
	xaint sz)
		sl;		unt sl xfsbbt
 *(TS(idiv(len, maxref = xf!= Xtmp_logflagsnt0}; umbial log flagn nts list  rk)o	 _read(sFS_F=sb_agcBREehiBMAP_BTREE_Rf#inree leak->bbt
 *(tp_oTS(iflip', tv;	/a &FS_,
ror;0, &tmp_logflagst div(len, maxrecs)O*logflagsp*|= tmp_logflagsn
l	BLOCK(bp);
		XS_WANde ande}
bm cucleaevel r* bt	de "ode	*lormansde auec_t	i  Xews lprnya del.TNUM;
		sFS_; = xfFS_ the private.b.	de "ode	*BTTREforFS_bp) FS_ de}
bmREE_Rf#inor nul_u.l		xfs_ino*FS_at TS(idiv(len, maxre;
de a:
O*logflagsp*|= rst nyt  be16_dex ino#unfs_;LEFTo#unfs_;RIGHTm#unfs_;PREVpat will ba nts_e/a hole/ */RKrr */
	i	de "od{deumh of_fileofvoidf_t		of#inadd/t			le_hole_rr */(	cl;		/* data or attxtent length */
ct xfs_btreebp_rrk_t		*ifor dx, _dmwhichfoXFS_BM* */ indir/ntsertT sz)		*peleasie = 0;	*(iw)	 "bXew rrent */Rddpentfilint			leve	xf{tree level, for checkingnt */
lock ct xfs_btreebp_releasie = 0;	ver ;
	xfver  Xeextbtnuwhichfobnoint sz)		*pfilblksllf	newlen=0;s inXew isdie =ti -
	t sz)		*pfilblksllf	oldlen=0;s inoldpisdie =ti -
	t sz)		*peleasie = 0;	rextn;s in extnuXeextbtnuwhichfobnoint sz)!= XFS_ the;  _dm nuosspiev,macc
	x.
iouruevel);sbtre)		*pfilblksllf	temp=0;s intempif rbrndie =ticalculod{desbtre
Su(*
	 * Root level must div(len, maxrecsS_ theT_B0;
	_read(sis		wh_rec.xfs_b(newblock numbxfs_b)ceyn)ng the ed a p = 0set flags ->t_i n"segm, i
haxfafver  XeextbtnEXTNUM;
		s*Tdxp>inc = xf_ theT|=6_to_cLEFTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pa*Tdxp-			, &ver ecsn) 	/ (,n		wh_rec.xfs_b(ver UP_G_rec.pSERT)d
	xf_ theT|=6_to_cLEFTbDELAY;dee 
eng the ed a p = 0set flags ->t_itecS_r, i
( extn)"segm, i
ex;	/vXEXTN If	i  doesn'tbex;	/,
we'p,  mbts_e{
		ttrohole/afobnd-of-filiXEXTNUM;
		s*Tdxp< TSckingf.LLFS;
	Ax/ (fl_f) -
	of(bp_release = 0)	 = xf_ theT|=6_to_cRIGHTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pa*Tdx	, & extn)csn) 	/ (,n		wh_rec.xfs_b( extnUP_G_rec.pSERT)d
	xf_ theT|=6_to_cRIGHTbDELAY;dee 
eng the Set cmbeiguityuflags ST ne over   = 0_extnuXeextbtn . the Don'tbletnt			leveget _ontntrgo, even ->t_itepiec
	bmp,  mbeigu enXEXTNUM;
		s(_ theT&6_to_cLEFTbVALIDfv&&b(_ theT&6_to_cLEFTbDELAY;bli_b]]]]ver UP_G_rec.b.br+ ver UP_Gxfs_bat);
}=_Cetwblock numbers li_b]]]]ver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
r<_CMAX;
}LEN)
FS_ theT|=6_to_cLEFTbCONTIG;Mrnu(bl(_ theT&6_to_cRIGHTbVALIDfv&&b(_ theT&6_to_cRIGHTbDELAY;bli_b]]]]newblock numbers +Cetwblockxfs_bat);
}=_C extnUP_G_rec.ers li_b]]]]etwblockxfs_bat);
}+C extnUP_Gxfs_bat);
r<_CMAX;
}LENbli_b]]]]p!(_ theT&6_to_cLEFTbCONTIG)Fel != 0]](ver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
r+m_       extnUP_Gxfs_bat);
r<_CMAX;
}LEN))e
FS_ theT|=6_to_cRIGHTbCONTIG;Mrnng the Sreset el *basxf	ST ne ocmbeiguityuflagsXEXTNUM; reset (_ theT&6(_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG)e = x dela_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
c		 */
		Niwa	de "od{deut numbeigu entec_t	rr */
	i	de "od{des */
		ST ne over   = 0ST ne o extnU */
		Mergolrevickd
xein */RKs{
	le t			le e =ordk, i, sz);--*Tdx;sxf
emp_BTver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
r+m_;	rextnUP_Gxfs_bat);
;ss;n
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pa*TdxB,itempXcs;
oldlen_BT_rec.pSERTst (ver UP_G_rec.pSERT)r+m_;	_rec.pSERTst (newblock numbxfs_b)r+m_;	_rec.pSERTst ( extnUP_G_rec.pSERT)cs;
newlen_BTl;		unt swoip'_isdleneTS(itempXcs;
 = xuneas_upd_rec.xfs_b({
		i Nock, i	xf(DR pa*TdxB,ee		 	wh_rec.xfs_b(tt_f)newlen))ey O
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_cey
 O = xi Nocremovrei pa*Tdxp+		, 1, e theXLEVAing key x dela_to_cLEFTbCONTIG:;
c		 */
		Niwa	de "od{deut numbeigu entec_t	RKrr */
	i	de "od{de */
		ST ne over U */
		Mergol
	in(iwa	de "od{detec_t	
	inver  Xeextbtn. ti, sz);--*Tdx;sxf
emp_BTver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
;ss;n
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pa*TdxB,itempXcs;
oldlen_BT_rec.pSERTst (ver UP_G_rec.pSERT)r+m_;	_rec.pSERTst (newblock numbxfs_b)cs;
newlen_BTl;		unt swoip'_isdleneTS(itempXcs;
 = xuneas_upd_rec.xfs_b({
		i Nock, i	xf(DR pa*TdxB,ee		 	wh_rec.xfs_b(tt_f)newlen))ey O
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceyleing key x dela_to_cRIGHTbCONTIG:;
c		 */
		Niwa	de "od{deut numbeigu entec_t	RKrr */
	i	de "od{de */
		ST ne o extnU */
		Mergol
	in(iwa	de "od{detec_t	
	in_extnuXeextbtn. ti, sz);
#inccl;		unt sprel indirei pa*Tdx, e the, _THI		IP_cey O
emp_BTetwblockxfs_bat);
}+C extnUP_Gxfs_bat);
cs;
oldlen_BT_rec.pSERTst (newblock numbxfs_b)r+m_;	_rec.pSERTst ( extnUP_G_rec.pSERT)cs;
newlen_BTl;		unt swoip'_isdleneTS(itempXcs;
 = xuneas_upd	def({
		i Nock, i	xf(DR pa*TdxB,ee		 Cwblock numbers,ee		 	wh_rec.xfs_b(tt_f)newlen)(itemp,C extnUP_G_rehe)ey O
#inccl;		unt spo_al indirei pa*Tdx, e the, _THI		IP_ceyleing key x dela0:;
c		 */
		Niwa	de "od{deut nno= umbeigu entec_t	Rno=her */
		rr */
	i	de "od{deum_	r.
ItsertTar(iwabnoin. ti, sz);oldlen_BTnewlen_BT0;s O = xi Nocinsertei pa*Tdx, 1, etw, e theXLEleing key	} lu(bloldlen_!BTnewlen	 = xf_read(soldlen_>Tnewlen	;s O = xicsbep;dify_at);
ers(TSckin_broo(  = mSB	bpDnt);
S(erro(intCK(m)soldlen_- newlen)(i0BROO			 */
		No=h{
		to doif rbdisk quotamaccbroo{
		hereum_	r./y	} at will ba nts_e/a hole/ */RKreal ede "od{de.mh of_fileoff_tur,
	xfs_fileoff_t		of#inadd/t			le_hole_			d(	c
	uint			ofof#de "o	*of#,z)!= XFSwhichlock)f{trs_fsblock_teleasie =	*(iw_BT&uma->go
cs;	/* index into the extents list */
	xfs	/* ini, 0};tempie theT btree level, for checkingnt */
lock ct xfs_btreebp_releasie = 0;	ver ;
	xfver  Xeextbtnuwhichfobnoint sz)		*peleasie = 0;	rextn;s in extnuXeextbtnuwhichfobnoint sz)!= XFSrst =0;s in nts list */
(logg{
		flags)	leve!= XFS_ the;s in nuosspiev,macc
	x.
iouruevel);sbtre
Su(*
	 * Root level muma->TS(iwhichlock)csn) < mp->mma->Tdxp> Xncey _read(smma->Tdxp<ADDR macro ;
	A /  -
	of(s_fsblock_telease =	cey _read(s is		wh_rec.xfs_b(newblock numbxfs_b)cey _read(s mma->FS_Fel != 0]]  !ouma->FS_ the private.b.flags & * RoBTCUR_BPRV_WASDEL)ceyn) = mSfilRo		C(xs
add/t	l;	/)t;
S_ theT_B0;
	u(blwhichlock __eturnATTR maxre
FS_ theT|=6_to_cATTRmaxr;Mrnng the ed a p = 0set flags ->t_i n"segm, i
haxfafver  Xeextbtn. theUM;
		smma->Tdxp>inc = xf_ theT|=6_to_cLEFTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pamma->Tdxp-			, &ver ecs) 	/ (,n		wh_rec.xfs_b(ver UP_G_rec.pSERT)d
	xf_ theT|=6_to_cLEFTbDELAY;dee 
eng the ed a p = 0set flags ->t_i n"segm, i
haxfafcS_r, i
st */. the No=btrlxei*/we'p, rnsert{ons lWANne o"hole"/afobof. theUM;
		smma->Tdxp<DDR macro ;
	A / (fl_f) -
	of(bp_release = 0)	 = xf_ theT|=6_to_cRIGHTbVALID;s;nbp_releasgupdate({
		i Nock, i	xf(DR pamma->TdxB, & extn)cs) 	/ (,n		wh_rec.xfs_b( extnUP_G_rec.pSERT)d
	xf_ theT|=6_to_cRIGHTbDELAY;dee 
eng the We'p, rnsert{onsRKreal ede "od{detbetween_"ver "p = 0" extn". the Set ne ocmbeiguityuflagsX  Don'tbletnt			leveget _ontntrgoXEXTNUM;
		s(_ theT&6_to_cLEFTbVALIDfv&&b!(_ theT&6_to_cLEFTbDELAY;bli_b]]]]ver UP_G_rec.b.br+ ver UP_Gxfs_bat);
}=_Cetwblock numbers li_b]]]]ver UP_G numbh "le +Cver UP_Gxfs_bat);
}=_Cetwblock numbh "le li_b]]]]ver UP_G nuheT__Cetwblock nuhe li_b]]]]ver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
r<_CMAX;
}LEN)
FS_ theT|=6_to_cLEFTbCONTIG;Mrnu(bl(_ theT&6_to_cRIGHTbVALIDfv&&b!(_ theT&6_to_cRIGHTbDELAY;bli_b]]]]newblock numbers +Cetwblockxfs_bat);
}=_C extnUP_G_rec.ers li_b]]]]etwblock numbh "le +Cetwblockxfs_bat);
}=_C extnUP_G_rec.h "le li_b]]]]etwblock nuhe =_C extnUP_G_reheT&i_b]]]]etwblockxfs_bat);
r+  extnUP_Gxfs_bat);
r<_CMAX;
}LENbli_b]]]]p!(_ theT&6_to_cLEFTbCONTIG)Fel != 0]]ver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
r+m_      extnUP_Gxfs_bat);
r<_CMAX;
}LEN))
FS_ theT|=6_to_cRIGHTbCONTIG;MrneBMAP_BTTREeng the Sel =tiwhichnudelawe'p, rn	here,p = 0-mplem, i
itXEXTNUM; reset (_ theT&6(_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG)e = x dela_to_cLEFTbCONTIG |6_to_cRIGHTbCONTIG:;
c		 */
		Niwa	de "od{deut numbeigu entec_t	real ede "od{des ST ne  */
		ver   = 0ST ne o extnU */
		Mergolrevickd
xein */RKs{
	le t			le e =ordk, i, sz);--mma->Tdxey O
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pamma->TdxB,
	xfver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
r+m_;	rextnUP_Gxfs_bat);
)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_cey
 O = xi Nocremovreuma->TS(imma->Tdxp+		, 1, e theXLEEVA* Root levNEXT_SETmuma->TS(iwhichlock,delevel)ot levNEXTENTSmuma->TS(iwhichlock)p-			cs) 	/ (mma->FS_F=sb_agcB = xf;rst 
	 * RooLOG (bpE |6 = xilog_f	xf(whichlock)cs		} if ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_,  extnUP_G_rec.ers,error; extnUP_G_rec.h "le,  extnUP_Gxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rip,		deleireuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rip,		decremt	n_uma->FS_, 0, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, ver UP_G_rec.b.b,error;ver UP_G numbh "le,error;ver UP_Gxfs_bat);
r+m_;				etwblockxfs_bat);
}+ee		r;	rextnUP_Gxfs_bat);
,error;ver UP_G nuhe_bytl	BLOCK(bp);
		X	S_WANde andel} leing key x dela_to_cLEFTbCONTIG:;
c		 */
		Niwa	de "od{deut numbeigu entec_t	RKreal ede "od{de */
		ST ne over U */
		Mergol
	in(iwa	de "od{detec_t	
	inver  Xeextbtn. ti, sz);--mma->Tdxey O
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_updxfs_bat);
 {
		i Nock, i	xf(DR pamma->TdxB,
	xfver UP_Gxfs_bat);
}+Cetwblockxfs_bat);
)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_cey
 O	/ (mma->FS_F=sb_agcB = xf;rst 
	  = xilog_f	xf(whichlock)cs		} if ( = xf;rst 
	 0m
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_, ver UP_G_rec.b.b,error;ver UP_G numbh "le,]ver UP_Gxfs_bat);
,error;&i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, ver UP_G_rec.b.b,error;ver UP_G numbh "le,error;ver UP_Gxfs_bat);
r+m_;				etwblockxfs_bat);
,error;ver UP_G nuhe_bytl	BLOCK(bp);
		X	S_WANde andel} leing key x dela_to_cRIGHTbCONTIG:;
c		 */
		Niwa	de "od{deut numbeigu entec_t	RKreal ede "od{de */
		ST ne o extnU */
		Mergol
	in(iwa	de "od{detec_t	
	in_extnuXeextbtn. ti, sz);
#inccl;		unt sprel indireuma->TS(imma->Tdx, e the, _THI		IP_cey O = xuneas_upd	def({
		i Nock, i	xf(DR pamma->TdxB,ee		 Cwblock numbers,a Cwblock numbxfs_b,
or; Cwblockxfs_bat);
}+ rextnUP_Gxfs_bat);
,erro extnUP_G_rehe)ey O
#inccl;		unt spo_al indireuma->TS(imma->Tdx, e the, _THI		IP_cey
 O	/ (mma->FS_F=sb_agcB = xf;rst 
	  = xilog_f	xf(whichlock)cs		} if ( = xf;rst 
	 0m
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_,error; extnUP_G_rec.ers,error; extnUP_G_rec.h "le,error; extnUP_Gxfs_bat);
, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		X_BMAP_BTREE_Rff,
 indireuma->FS_, etwblock numbers,error; Cwblock numbxfs_b,
;				etwblockxfs_bat);
}+ee		r;	rextnUP_Gxfs_bat);
,error; extnUP_G_rehe)ey O	BLOCK(bp);
		X	S_WANde andel} leing key x dela0:;
c		 */
		Niwa	de "od{deut nno= umbeigu entec_t	Rno=her */
		real ede "od{de.m_	r.
ItsertTar(iwabnoin. ti, sz); = xi Nocinserteuma->TS(imma->Tdx, 1, etw, e theXLEle* Root levNEXT_SETmuma->TS(iwhichlock,delevel)ot levNEXTENTSmuma->TS(iwhichlock)p+			cs) 	/ (mma->FS_F=sb_agcB = xf;rst 
	 * RooLOG (bpE |6 = xilog_f	xf(whichlock)cs		} if ( = xf;rst 
	 * RooLOG (bpEm
		X_BMAP_BTREE_Rff,
	xfs_fseqsmma->FS_,error; Cwblock numbers,error; Cwblock numbxfs_b,
;				etwblockxfs_bat);
, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
		0nsde a)m
		Xmma->FS_ the e =UPUP_G_reheT_eetwblock nuhe;oFOe_BMAP_BTREE_Rip,		inserteuma->FS_, &i_bytl	BLOCK(bp);
		X	S_WANde andelevel),
			error0);
		if (i-
			nsde a)m
		} leing key	} bm cuc nts_e/ */RKoid
xei*/
ec
	xaint sz)
		sl;		unt sl xfsbbt
 *(uma->TS(iwhichlock)f = xf!= Xtmp_logflagsnt0}; umbial log flagn nts list  rk)o	 _read(smma->FS_F=sb_agcBREehiBMAP_BTREE_Rf#inree leak->bbt
 *(uma->tp_ouma->TS(erroruma->t = {pSERT( uma->tv;	/a &uma->FS_,error0, &tmp_logflagst whichlock)cs		uma->logflags
|= tmp_logflagsn
l	BLOCK(bp);
		XS_WANde ande}
bm cucleaevel r* bt	de "ode	*lormansde auec_t	i  Xews lprnya del.TNUM;
		smma->FS_;
		mma->FS_ the private.b.	de "ode	*BTTREOOREE_Rf#inor nul_u.l		xfs_inouma->FS_, uma->TS(iwhichlock)csde a:
Ouma->logflags
|= rst nyt  be16_dex inoat will bFuncd{des us
	iisr* btt			le e aans	de "ode  = 0_emovr; uthsmh of will bAdjl = * bts-
	t f	
	in(iwat			le*basxf	ST di		xfs-
	t = 0_tnt		s-
	.mh of!= 
REE_Rf#inrees-
	d	dign(	cl;		_broollf mp,	cl;		eleasie = 0;*S_Wp,	X "bXehi whichfoct xfs_btreebp_releasie = 0;* lasp,	X "b last entwhichfoct xfs_btreebp_rwhilen 0;reesz,	X "b	dignbWANneintwhichfo -
	t sz)!= XFrt,	X "bisNneintRKrealgime nt */?t sz)!= XFeof,	X "bisNwhichfoafobnd-of-fili?t sz)!= XFrr */,	X "bcreod ng	rr *de "Nwhichf?t sz)!= XFc nts_e, _dmovrrwrif ng	unwrifdeniwhichf?t sz)		*pfilexntst_*xntp,	X "bin/el :b	dignequntp->iotreebp_rwhilen 0;*lenp)	X "bin/el :b	dignequleng_t		xf{tree lfilexntst_o ex_oxf, 0};o exinaluntp->iotreebp_rwhilen 0;o ex_alen, 0};o exinaluleng_t		xfree lfilexntst_o ex_bnd, 0};o exinaluntp+len_	xfree lfilexntst_Xehio;	X "bXehi filinntp->iotreebp_rfilexntst_ laso;	X "b last entfilinntp->iotreebp_rfilexntst_	dign_oxf, 0};tempif rbntp->iotreebp_rwhilen 0;	dign_alen, 0};tempif rbleng_t		xfree lwhilen 0;temp;	X "btempif rbcalculod{desbtre
Su( (c nts_e;
		  be16_TREOOo ex_oxf*BT	dign_oxfT= *xntp;
;o ex_alen*BT	dign_alen*BT*lenp;
;o ex_rnqu= o ex_oxf*+ o ex_alen, 
eng the I>t_i n"reque	/*nts_laps/afiex;	/ ng	whichf,t* bnsde 't the attempe/ */perf rmprnyaaddid{deal edignmt	nXEXTNUM;
		s!rr */v&&b!exnbli_b]]]]po ex_oxf*>= S_Wpblock numbers)bli_b]]]]po ex_rnqu<= S_Wpblock numbers*+ S_Wpblockxfs_bat);
)f = xf  be16_TREee 
eng the If	ne obllinntp->io n"un	dignequvs.r* btt			le  -
	
	r.
webl xf/ */Rdignbi,u 
Ti n"willmap_po_sibockunl
	x
	r.
ne obllinwasb last enly wrifdeniec_t	RKkernevickafodid 't the perf rmpneintRdignmt	n,t btiDoaotruncode sho= entnt ne  *he foonXEXTNUM;
emp_BTdoep;dpo ex_oxf,nt		sz);
	u(bltempX = xf	dign_alen*+BT
emp; xf	dign_ers -BT
emp; x}Eeng the Same adjl =m, i
f rb* bttnqunt	
	in_eque	/
	i	reoXEXTNUM;
		s(
emp_BT(	dign_alen*%nt		sz))X = xf	dign_alen*+BTt		szp-itemp; x}Eeng the IDone o last enth "le nts_laps/ec_t	
	isb lopo_
	i	de "od{de *r.
ne n movr;* btsnumb
f rward/ec_tel radjl ={
		ttroleng_tXEXTNUM;
		s laspblock numbers*!sb_agcFILEOFFf = xf!		s laspblock numbxfs_b}_= HOLESTARTnt);
;
		X laso_BT laspblock numbersREehif (
		X laso_BT laspblock numbers*+  laspblockxfs_bat);
cs;} if (
	X laso_BT0;
	u(bl	dign_ers != o ex_oxf*&&b	dign_ers <  laso) xf	dign_ers BT laso;Eeng the IDone oXehi h "le nts_laps/ec_t	
	isb lopo_
	i	de "od{de *r.
ne n movr;* btsnumb
bale ec_tel radjl ={
		ttroleng_t, *r.
butnno= beflengntp->io0. the Ti n"m*/vnt	at)r ( m voickinsnumb
nts_lapo last enth "le, *r.
 = 0-*/we hi= * btntp->io0 limi= * bnone oXehi h "le *r.
cafi ={de* ts_lapotooXEXTNUM;
		s!exnbli S_Wpblock numbers*!sb_agcFILEOFFf = xf!		s(rr */v&&bS_Wpblock numbxfs_b}_= HOLESTARTnt);
;Fel !b]]]]p!rr */v&&bS_Wpblock numbxfs_b}_= DELAYSTARTnt);
;)
or; Chio = S_Wpblock numbers*+ S_Wpblockxfs_bat);
REehif (
		X Chio = S_Wpblock numberscs;} if (
	X Chio = _agcFILEOFF;M;
		s!exnbli_b]]]]	dign_ers +T	dign_alen*!= o ex_tnquli_b]]]]	dign_ers +T	dign_alen*>oXehio) xf	dign_ers BT Chio >T	dign_alen*?T Chio -T	dign_alen*:TTREeng the I*/we'p, Xews ts_lapp{
		ttroXehi  rb last entwhichfockaf the means
webcan'tbfit/afiex	szppiec
tnt ne n"holeu 
Jl = movr
	r.
ne osnumb
f rward/WANne oblip'ist id spotp = 0set
	r.
ne oleng_t	so/we hi= * bttnqXEXTNUM;
		s	dign_ers != o ex_oxf*&&b	dign_ers <  laso) xf	dign_ers BT laso;Ee
		s	dign_ers +T	dign_alen*!= o ex_tnquli_b]]]]	dign_ers +T	dign_alen*>oXehioT&i_b]]]]ethioT!sb_agcFILEOFFf = xf_read(s Chio >T laso); xf	dign_alen*=T Chio -T	dign_erscs;} 
eng the If	realgime,p = 0
	in_esulio nn'tba muliipleunt	
	in_ealgime the to next -
	twebl xf/ */_emovr;xfs_bn"un={d	i  inXEXTNUM;
		srtv&&b(
emp_BT(	dign_alen*%nmpblm_sb.sb_rrees-
	))X = xf		 */
		We'p, Xe= umts_{
		ttroo exinalu_eque	/,t b
/	r.
webwon'tbbet	ble/ */onc	twebfix	ttroleng_tXEXi, sz);u(blo ex_oxf*<]	dign_ers el !b]]]]o ex_tnqu>]	dign_ers +T	dign_alen*el !b]]]]	dign_alen*-itemp*<]o ex_alenB
XFSr be16_-EINVALROO			 */
		Trypentfix	i  by mov{
		ttrosnumb
upXEXi, sz);u(bl	dign_ers +Ttemp*<= o ex_oxfB = xf;	dign_alen*-BT
emp; xff	dign_ers +BT
emp; xf}OO			 */
		Trypentfix	i  by mov{
		ttrotnquieum_	r./
;
if ( 
		s	dign_ers +T	dign_alen*-itemp*>= o ex_tnq) xf;	dign_alen*-BT
emp; xf		 */
		Set	ttrosnumb
WANne omt({mum * bnonrim	ttroleng_tXEXi, sz);if ( = xf;	dign_alen*-BTo ex_oxf*-T	dign_erscs;xf	dign_ers BTo ex_oxf, xf;	dign_alen*-BT	dign_alen*%nmpblm_sb.sb_rrees-
	; xf}OO			 */
		Resuliodoesn'tbumts_	
	in_eque	/, fa{d	i XEXi, sz);u(blo ex_oxf*<]	dign_ers el]o ex_tnqu>]	dign_ers +T	dign_alenB
XFSr be16_-EINVALROO} if ( = xf_read(so ex_oxf*>= 	dign_erscey O_read(so ex_rnqu<= 	dign_ers +T	dign_alenBcs;} 
#ifdef DEBUGM;
		s!exnbli S_Wpblock numbers*!sb_agcFILEOFFfy O_read(s	dign_ers +T	dign_alen*<= S_Wpblock numbers);
	u(bl laspblock numbers*!sb_agcFILEOFFfy O_read(s	dign_ers >BT laspblock numbers*+  laspblockxfs_bat);
);
#rnqif

;*lenp*BT	dign_alen;
	*xntp*BT	dign_oxfnyt  be16_0noat #fs_ineeturnALt);_GAP_UNITS	4

voidf_t		of#inadjact	n_	c
	uint			ofof#de "o	*ap)	 "bof#ii	de "i	rgumchfoe uint		xf{tree lfsxfs_b 0;	djl =;	X "badjl =m, i
torxfs_b}XFS_BMsbtre)		*pagXFS_BM 0;fbpagXo, 0};agoXFS_BM* Doap->t = {pSERTbtre)		*p_broollf mp;	X "b_broooct xfoe uintur	t sz)!= XF		whfb;	X "btrlxei*/ap->t = {pSERTb nn'tb->iotree!= XFrt;	X "btrlxei*/nt */
 n"realgime tre
#fs_ine	ISVALID(x,y)	\
	srtv? \
		(x)*<]mpblm_sb.sb_rxfs_bn": \
		turnFSB_TO_AGNO(mp,Cx) __eturnFSB_TO_AGNO(mp,Cyfv&&b\
		turnFSB_TO_AGNO(mp,Cx) <]mpblm_sb.sb_agat);
}&&b\
		turnFSB_TO_AGBNO(mp,Cx) <]mpblm_sb.sb_agxfs_bn)

;mp_BTap->TSckin_broonyt		whfb*BT*ap->t = {pSERTb=sb_agcFSBt);
nyt t
	 * RooS_REALTIME_INODE(ap->TSfv&&bap->userrrennytfbpagXo*=T 	whfb*?b_agcAGNUMBER*: turnFSB_TO_AGNO(mp,C*ap->t = {pSERT)REeng the I*/	de "od{onsRfobof,p = 0
	ire'soao last entreal h "le, *r.
trypentu ( 
tsolter pSERTbas t)rosnumb{onsct xfXEXTNUM;
		s	p->exnbli 	p-> las.ock numbers*!sb_agcFILEOFFT&i_b]]]] is		wh_rec.xfs_b(	p-> las.ock numbxfs_b)r&i_b]]]]ISVALID(	p-> las.ock numbxfs_b +T	p-> las.ockxfs_bat);
,err]]]]	p-> las.ock numbxfs_b)X = xf	pblolkXo*=T	p-> las.ock numbxfs_b +T	p-> las.ockxfs_bat);
; xf		 */
		Adjl = f rb* btg#iibetween_ laspp = 0enk, i, sz);adjl =*=T	p->ntp->io-
XFS(	p-> las.ock numbers +T	p-> las.ockxfs_bat);
	cs) 	/ (adjl =*&i_bb]]]]ISVALID(	p->olkXo*+badjl =,]	p-> las.ock numbxfs_b)X xf;	p->olkXo*+=badjl =; x}Eeng the IDoXe= Rfobof,p* bnocompmp, * bttwouXeextbtnuxfs_bn. the Figur	tel rwheTIC ieiTIC ie augives usoaogoodosnumb{onsct xf, *r.
 = 0pi_b * btbettC ie aXEXTNUM;if ( 
		s!	p->exnX = xfee lfsxfs_b 0;S_Wbno;	X "b_extnusiderxfs_b}XFS_BM, sz); = xfsxfs_b 0;S_Wqiff=0;s in extnusiderqiffirenc	t sz); = xfsxfs_b 0; lasbXo, 0};ver  siderxfs_b}XFS_BM, sz); = xfsxfs_b 0; lasqiff=0;s inver  siderqiffirenc	t sz xf		 */
		IDone re'soao last ent(ver ) h "le,b->l =tian_eque	/
	 */
		snumb
bfs_b}basxf	ST i XEXi, sz);u(bl	p-> las.ock numbers*!sb_agcFILEOFFT&i_bb]]]] is		wh_rec.xfs_b(	p-> las.ock numbxfs_b)r&i_bb]]]]p lasbXo*=T	p-> las.ock numbxfs_b + xf;= 0]]  	p-> las.ockxfs_bat);
	*&i_bb]]]]ISVALID( lasbXo,]	p-> las.ock numbxfs_b)X = xf;		 */	r.
Calculodbtg#iienttnqunt	 last enth "le. */	r.t ur,adjl =*=T lasqiff*=T	p->ntp->io-
XFSS(	p-> las.ock numbers +
XFSST	p-> las.ockxfs_bat);
	cs) ;		 */	r.
Figur	tttrosnumbbfs_b}basxf	ST ne o last enth "le's */	r.
tnqu = 0
	ing#iis-
	.m*/	r.
Heur;	/ c!m*/	r.
IDone og#iiistntrgon_elod{vebWANne o iec
twe'p,m*/	r.
	de "od{on,t btus{ons tugives usoaT inst id h "le **/
		eFS_BM,p* bnojl =*u ( * bttnqunt	
	in last enth "le. */	r.t ur,u(bl lasqiff*<=eturnALt);_GAP_UNITSr.
	p->leng_t	&i_bbb]]]]ISVALID( lasbXo*+  lasqiff,error]]]]	p-> las.ock numbxfs_b)Xerror lasbXo*+=badjl =; xehif (
		X; lasqiff*+=badjl =; xeh		 */	r.
If	ne obl= {pSERTbf rbidss t,bcan'tbu ( 
t, */	r.
ml =*u ( fs_auli. */	r.t ur,u(bl!rtv&&b! 	whfb*&i_bbb]]]]turnFSB_TO_AGNO(mp,C lasbXohfat fbpagXoXerror lasbXo*sb_agcFSBt);
nytf}OO			 */
		Noo last enth "le nrbcan'tbfode ws t,bjl =*fs_auli. */, sz);if (
ror lasbXo*sb_agcFSBt);
nytf		 */
		IDone re'soaofode w{ons( extn)"h "le,b->l =tian_eque	/
	 */
		snumb
bfs_b}basxf	ST i XEXi, sz);u(bl is		wh_rec.xfs_b(	p->S_W.ock numbxfs_b)X = xf;		 */	r.
Calculodbtg#iientsnumb
nfoXehi h "le. */	r.t ur,adjl =*=TS_Wqiff*=T	p->S_W.ock numboxf*-T	p->ntp->ics) ;		 */	r.
Figur	tttrosnumbbfs_b}basxf	ST ne oXehi h "le's */	r.
snumb
 = 0
	ing#iis-
	.m*/	r.t ur,S_Wbno*=T	p->S_W.ock numbh "lecs) ;		 */	r.
Heur;	/ c!m*/	r.
IDone og#iiistntrgon_elod{vebWANne o iec
twe'p,m*/	r.
	de "od{on,t btus{ons tugives usoaT inst id h "le **/
		eFS_BM,p* bnojl =*u ( * btsnumb
nfone oXehi h "le **/
		Stp->ioby t)roleng_tXEXi	r.t ur,u(blS_Wqiff*<=eturnALt);_GAP_UNITSr.
	p->leng_t	&i_bbb]]]]ISVALID(S_Wbno*-TS_Wqiff, S_Wbno);
		X	S_Wbno*-=badjl =; xehif ( 
		sISVALID(S_Wbno*-T	p->leng_t, S_Wbno); = xf;	S_Wbno*-=bap->leng_t; xf;	S_Wqiff*+=badjl =*-T	p->leng_t; xf;} if (
	X;	S_Wqiff*+=badjl =; xeh		 */	r.
If	ne obl= {pSERTbf rbidss t,bcan'tbu ( 
t, */	r.
ml =*u ( fs_auli. */	r.t ur,u(bl!rtv&&b! 	whfb*&i_bbb]]]]turnFSB_TO_AGNO(mp,CS_Wbno)fat fbpagXoXerrorS_Wbno*=T_agcFSBt);
nytf}OO			 */
		NooXehi h "le,bjl =*fs_auli. */, sz);if (
rorS_Wbno*=T_agcFSBt);
nytf		 */
		IDobo_t	st id,0pi_b * btbettC ie a, if ( ttroonlyogood
*/
		STa, if ( 	p->olkXo*intRde aayb->io(WAN0  rb* btnt */
xfs_b)XEXi, sz);u(bl lasbXo*!=T_agcFSBt);
bli S_WbXo*!=T_agcFSBt);
X xf;	p->olkXo*=T lasqiff*<=TS_Wqiff*?C lasbXo*: S_Wbno;
;
if ( 
		s lasbXo*!=T_agcFSBt);
X xf;	p->olkXo*=T lasbno;
;
if ( 
		sS_WbXo*!=T_agcFSBt);
X xf;	p->olkXo*=TS_Wbno;
;}m#unfs_]ISVALIDoat  nuhic != 
REE_Rf#inlonge	/_fp,		to nex_	c
	uint			oftrans	*tp,	cl;		agXFS_BM 0;	ag,free lwhilen 0;	*blen,z)!= XFS*Xe=init)f{trs_fsblock_t_broof mp*=TWpbltt_broop; xs_fsblock_tperag	*pag;free lwhilen 0;	longe	/;s;	/* index i*BTTREOOpag_BTREE_peragck, (mp,Cag);
	u(bl!pag-> agf_init) = xfiBMAP_BTREE_	de "_ agf_init(mp,Ctp,Cag,eturnALt);_FLAG_TRYt);
Xn
l	BLOCK(bp);
		XS_WANel ey
 O	/ (!pag-> agf_init) = xfS*Xe=init_BT1;
		XS_WANel eytf}OO} 
elonge	/_BTREE_	de "_longe	/_fp,		to nex_mp,C ag);
	u(bl*blen < longe	/;
		*blen = longe	/;s
el :free lperagcput( ag);
	  be16_dex inoat  nuhic voidf_t		of#in->l =t_minlene	c
	uint			ofof#de "o	*ap,	c
	uint			of	de "_trg	*args,free lwhilen 0;	*blen,z)!= XFSXe=init)f{tru(blXe=init_|| *blen < 	p->minlenX = xf		 */
		Sinc	twebdid a BUF_TRYt);
t	beva, i  in_po_sibockckaf t	r.
ne r/
 n"space f rb*  n"reque	/k, i, sz);args->minlen*=T	p->minlenROO} if ( u(bl*blen < args->maxlenX = xf		 */
		If	ne obe	/_seen_leng_t	istnessNnean	
	in_eque	/oleng_t, */
		u ( * btbe	/_asNne omt({mumk, i, sz);args->minlen*=T*blenROO} if ( = xf		 */
		One rwielawe'vebseen_anNwhichfoanthigoantmaxlen,	u ( * af t	r.
asNne omt({mumk, i, sz);args->minlen*=Targs->maxlen;y	} at _fileoff_tf_t		of#inbt	de "_ 	whfbe	c
	uint			ofof#de "o	*ap,	c
	uint			of	de "_trg	*args,free lwhilen 0;	*blen)f{trs_fsblock_t_broof mp*=Tap->TSckin_broonytl;		agXFS_BM 0;	ag,tsnumbag;fr!= XFSXe=init_BT0;
	u/* index in 
eargs->typeT_eturnALt);TYPE_START_BNO;
eargs->tott 
	 ap->tott t;
S_ tmbag
	 ag _eturnFSB_TO_AGNO(mp,Cargs->fsbno);
	u(bl_ tmbag
	=b_agcAGNUMBER)
FS_ tmbag
	 ag _eTREOOwhllinl*blen < args->maxlenX = xfiBMAP_BTREE_Rf#inlonge	/_fp,		to nex_args->tp,Cag,eblen,z)	rror]]]] &Xe=init)n
l	BLOCK(bp);
		X  be16_dex ino
l	BLOC++ag
	=bmpblm_sb.sb_agat);
X xf;	g
	 0m
		u(bl	g
	=b_ tmbagX xf;ing key	} bm_t		of#in->l =t_minleneap,Cargs,eblen, Xe=init)n
l  be16_0noat _fileoff_tf_t		of#inbt	de "_bllis_feamse	c
	uint			ofof#de "o	*ap,	c
	uint			of	de "_trg	*args,free lwhilen 0;	*blen)f{trs_fsblock_t_broof mp*=Tap->TSckin_broonytl;		agXFS_BM 0;	ag;fr!= XFSXe=init_BT0;
	u/* index in 
eargs->typeT_eturnALt);TYPE_NEAR_BNO;
eargs->tott 
	 ap->tott t;
Sag _eturnFSB_TO_AGNO(mp,Cargs->fsbno);
	u(blag
	=b_agcAGNUMBER)
FSag _eTREOOiBMAP_BTREE_Rf#inlonge	/_fp,		to nex_args->tp,Cag,eblen, &Xe=init)n
lBLOCK(bp);
		  be16_dex ino
lu(bl*blen < args->maxlenX = xfiBMAP_BTREE_bllis_feam_etwxageap,C&ag);
		BLOCK(bp);
		X  be16_dex ino
l	iBMAP_BTREE_Rf#inlonge	/_fp,		to nex_args->tp,Cag,eblen,z)	rror]]]] &Xe=init)n
l	BLOCK(bp);
		X  be16_dex ino
l} bm_t		of#in->l =t_minleneap,Cargs,eblen, Xe=init)n

eng the Set ne obailur	tf	debale  delaWAN	xfsiisr* bt->l =txf	AG
asNs_feam the may havebmovrqXEXTNUM;	pblolkXo*=T	rgs->fsbnoT_eturnAGB_TO_FSB(mp,Cag(i0BROO  be16_0noat _fileoff_tf_t		of#inbt	de "_	c
	uint			ofof#de "o	*ap)	 "bof#ii	de "i	rgumchfoe uint		xf{tree l_broollf mp;	X "b_broooct xfoe uintur	t sz)		of	de "type 0;	typeT_e0;s intypeTf rb	de "od{deurel inesbtre)		*pwhilen 0;	dign;	X "b_t({mum 	de "od{deuRdignmt	nbtre)		*pagXFS_BM 0;fbpagXo, 0};agoXFS_BM* Doap->t = {pSERTbtre)		*pagXFS_BM 0;ag;free l	de "_trg 0;argsn
l		*pwhilen 0;blenROO		*pwhilen 0;Xehiminlen*=T0;
	u/* i		whfb;	X "btrlxei*/ap->t = {pSERTb nn'tb->iotree!= XFis	digneq;
	u/* itryagain;
	u/* idex ino	u/* ie uip	d	digncsn) < mp->	p->leng_t)n

emp*=Tap->TSckin_broony
s in nuip	uRdignmt	nbf rb	de "od{deuistdetC minxf	by morooocarametC sbtre)e uip	d	dign_BT0;
	u(blmpblm_swid_t	&iblmpblm_flags & * RoMOUNT_SWALt);))
FS_ uip	d	dign_BTmpblm_swid_t;M;if ( 
		smpblm_d	dign)
FS_ uip	d	dign_BTmpblm_d	digncsn)	dign_BTap->userrren*?C		*pk, i	xfsz_hiex_ap->TSfv:T0;
	u(blunlikelys	dign)X = xfiBMAP_BTREE_Rf#inrees-
	d	dign(mp,C&ap->S_W,C&ap-> las,z)	rror	dign, 0, 	p->exn, 0, 	p->c nt,z)	rror&	p->ntp->i,C&ap->leng_t)n
 O_read(s!K(bp);n
 O_read(sap->leng_t)n
 at yt		whfb*BT*ap->t = {pSERTb=sb_agcFSBt);
nytfbpagXo*=T 	whfb*?b_agcAGNUMBER*: turnFSB_TO_AGNO(mp,C*ap->t = {pSERT)REeu(blX	whfbf = xf!		sap->userrren*&&bbp_r/* datiE_bllis_feam_ap->TSfB = xf;	g_BTREE_bllis_feam_	xfs_fsageap->TSf; xf;	g_BTlag
!=b_agcAGNUMBER)*?bag
: 0m
		X	pblolkXo*=TturnAGB_TO_FSB(mp,Cag(i0BROO	} if ( = xf;	pblolkXo*=TturnINO_TO_FSB(mp,Cap->TSckinino);
	f}OO} if (
	X	pblolkXo*=T*ap->t = {pSERTREOOREE_Rf#inadjact	n_ap), 
eng the I>t	de wed,	u ( 	pblolkXo; one rwielaml =*u ( t = {pSERTbsinc	 the it'ntnt ne n extnu	de "od{deugrelpXEXTNUM;
		s 	whfb*||eturnFSB_TO_AGNO(mp,CapblolkXo) __efbpagXoXerr;M;if (
	X	pblolkXo*=T*ap->t = {pSERTREeng the Normal ede "od{densde authrelgh			of	de "_vto nexXEXTNUM;
ryagain*=Tis	digneq_BT0;
	mem->i(&args,e0,  -
	of(args)cey args.tp*=Tap->op; xargs.mp*=Tmp; xargs.fsbnoT_e	pblolkXo;y
s inTrim	ttroede "od{detbale WANne omax{mum 	n	AG
cafifi,u NUM;	rgs.maxlenT_eMINsap->leng_t,eturnALt);_AG_MAX_USABLE(mp)cey args.t = {pSERTb=T*ap->t = {pSERTREeblen*=T0;
	u(blX	whfbf = xf		 */
		Searchbf rb	nu	de "od{deugrelpiec_t	RKs{
	le t			le ntrgo
/	r.
tnelgh	f rb* btreque	/k  I>te au nn'tbforod,p* bnoadjl = t	r.
ne b_t({mum 	de "od{deu -
	tWANne ontrgo	/_space f undXEXi, sz);u(bl	p->userrren*&&bbp_r/* datiE_bllis_feam_ap->TSfB
	xfiBMAP_BTREE_Rf#inbt	de "_bllis_feamseap,C&args,e&blen	;s Oif (
roriBMAP_BTREE_Rf#inbt	de "_ 	whfbeap,C&args,e&blen	;s OBLOCK(bp);
		X  be16_dex inoO} if ( u(blap->tv;	/->xbf_	xwf = xf!		sbp_r/* datiE_bllis_feam_ap->TSfB
	xfargs.typeT_eturnALt);TYPE_FIRST_AG;s Oif (
rorargs.typeT_eturnALt);TYPE_START_BNO;
erargs.tott 
	 args.minlen*=T	p->minlenROO} if ( {
orargs.typeT_eturnALt);TYPE_NEAR_BNO;
erargs.tott 
	 ap->tott t;erargs.minlen*=T	p->minlenROO}
s inapply to next -
	thiexs ->tobt	inxf	earliBM, sz)u(blunlikelys	dign)X = xfargs.prod*BT	dign;s OBLOC(args.mod*BT(		*pwhilen 0)doep;dp	p->ntp->i,Cargs.prod)fB
	xfargs.mod*BT(		*pwhilen 0)(args.prod*- args.mod)noO} if ( u(blmpblm_sb.sb_xfs_bn-
	t>= PAGE_CACHE_SIZEX = xfargs.prod*BT1t;erargs.moq_BT0;
	} if ( {
orargs.prod*BTPAGE_CACHE_SIZE >> mpblm_sb.sb_xfs_blog;s OBLOC(args.mod*BT(		*pwhilen 0)(doep;dp	p->ntp->i,Cargs.prod)fBB
	xfargs.mod*BT(		*pwhilen 0)(args.prod*- args.mod)noO}Eeng the I*/webmp, Xe= e wsdeuRvail	ble/rren*xfs_bn,p = 0
	i the unfsrly{onslogict 
volumcomanagenut nan nuip	,p = 
	r.
ne obllinntp->io n"zero * bnonry/ */Rde "ode rren *r.
bfs_bn"deu nuip	uunit_b undain. the NOTE:T	p->aexnbisoonlyo->io f	ttroede "od{detleng_t the ist>= * btsnuip	uunit_ = 0
	inede "od{detntp->io n the at * bttnqunt	filiXEXTNUM;
		s!ap->tv;	/->xbf_	xwbli 	p->aexnX = xf
		s!ap->ntp->iB = xf;	rgs.Rdignmt	nb= e uip	d	digncsxf;	typeT_eargs.typecsxf;is	digneq_BT1; xeh		 */	r.
Adjl = f rbRdignmt	n */	r.t ur,u(blblen*>o	rgs.Rdignmt	nbli blen*<= 	rgs.maxlenXerrorargs.minlen*=Tblen*-i	rgs.Rdignmt	n;
rorargs.minRdignslop
	 0m
		} if ( = xf;		 */	r.
Fiip'inry/anNwhant	bnoTede "od{de.m_		TN If	i  bailst* bnsdeTar(iaever	snumb
bno
*/	r.
	de "od{detec_t	Rdignmt	nbbe16xf	ST. */	r.t ur,atypeT_eargs.typecsxf;
ryagain*=T1;
rorargs.typeT_eturnALt);TYPE_THI		BNO;
er;	rgs.Rdignmt	nb= 1; xeh		 */	r.
Compute
ne b_t(len+Rdignmt	nbf rbne  *//
		eehi  del.T Set slop
soickafone bst */
**/
		Stb_t(len+Rdignmt	n+slop
doesn'tbg*/ i
**/
		between_ne oc	des. */	r.t ur,u(blblen*>o_ uip	d	dign_li blen*<= 	rgs.maxlenXerrorXehiminlen*=Tblen*-ie uip	d	digncsxf;if (
	X;	Xehiminlen*=Targs.minlen; ur,u(blXehiminlen*+o_ uip	d	dign_> args.minlen*+			errorargs.minRdignslop
	z)	rroXehiminlen*+o_ uip	d	dign_-
XFSSrargs.minlen*- 1; xehif (
	X;	args.minRdignslop
	 0m
		}
	} if ( {
orargs.Rdignmt	nb= 1; xeargs.minRdignslop
	 0m
	}
rargs.minleft*=T	p->minleftey args.wasde 
	 ap->wasde ey args.isf 
	 0m
	args.userrren*BTap->userrren;
	u(bl(iBMAP_BTREE_	de "_vto nex(&argsfBB
	x  be16_dex inoOu(bltryagain*li 	rgs.fsbnoT_=T_agcFSBt);
X = xf		 */
		Ehant		de "od{detbailed. Nowinry/ec_t	Rdignmt	n t	r.
ne16xf	ST. */, sz);args.typeT_eatypecsxfargs.fsbnoT_e	pblolkXo;yf;	rgs.Rdignmt	nb= e uip	d	digncsxfargs.minlen*=TXehiminlen; xeargs.minRdignslop
	 0m
	;is	digneq_BT1; xeu(bl(iBMAP_BTREE_	de "_vto nex(&argsfBB
	xX  be16_dex inoO}
 	/ (,n	digneq_li 	rgs.fsbnoT_=T_agcFSBt);
X = xf		 */
			de "od{detbailed,
soice16_oxf*Rdignmt	nb = 
	*r.
trypagain. */, sz);args.typeT_eatypecsxfargs.fsbnoT_e	pblolkXo;yf;	rgs.Rdignmt	nb= 0; xeu(bl(iBMAP_BTREE_	de "_vto nex(&argsfBB
	xX  be16_dex inoO}
 	/ (	rgs.fsbnoT_=T_agcFSBt);
_li  	whfb*&i_b]]]]	rgs.minlen*> 	p->minlenX = xfargs.minlen*=T	p->minlenROOrargs.typeT_eturnALt);TYPE_START_BNO;
erargs.fsbnoT_e	pblolkXo;yf;u(bl(iBMAP_BTREE_	de "_vto nex(&argsfBB
	xX  be16_dex inoO}
 	/ (	rgs.fsbnoT_=T_agcFSBt);
_li  	whfbX = xfargs.fsbnoT_e0ROOrargs.typeT_eturnALt);TYPE_FIRST_AG;s Oargs.tott 
	 ap->minlen; xeargs.minleft*=T0; xeu(bl(iBMAP_BTREE_	de "_vto nex(&argsfBB
	xX  be16_dex inoO	ap->tv;	/->xbf_	xwbBT1; x}
 	/ (	rgs.fsbnoT!=T_agcFSBt);
X = xf		 */
		or nu0
	inede "od{dethappeneq_afone bsame AP_higIC iAG
nean t	r.
ne bblip'ipSERTbckafowasb	de "ode	k, i, sz);_read(s*ap->t = {pSERTb=sb_agcFSBt);
*el !b]]]]]]]turnFSB_TO_AGNO(mp,C*ap->t = {pSERT)b=s !b]]]]]]]turnFSB_TO_AGNO(mp,C	rgs.fsbno)*el !b]]]]]]]lap->tv;	/->xbf_	xw*&i_bbbturnFSB_TO_AGNO(mp,C*ap->t = {pSERT)b<_bbbturnFSB_TO_AGNO(mp,C	rgs.fsbno))ceyn);	pblolkXo*=T	rgs.fsbno; xeu(bl*ap->t = {pSERTb=sb_agcFSBt);
B
	xX*ap->t = {pSERTb=T	rgs.fsbno; xe_read(s 	whfb*||efbpagXo*==T	rgs.agXo*el !b]]]]]]]lap->tv;	/->xbf_	xw*&iefbpagXo*<T	rgs.agXo))ey O	p->leng_t	=T	rgs.len; xeap->TSckind.di_nbfs_bn"+=T	rgs.len; xe		oftrans_logr/* da(ap->op,Cap->TS, * RooLOG (bpE	cs) 	/ (ap->wasde X xf;	p->TSckindr */
	_blks*-=bargs.len; xe		 */
		Adjl = ne bdisk quotamalso.
Ti n"wan"reserv
	 */
		earliBM. ti, sz); = xtrans_mod_dquot_by{oo(ap->op,Cap->TS, xf;	p->wasde 
? * RoTRANS_DQbDELBCOUNT :
XFSSr* RoTRANS_DQbBCOUNT(erro(long)bargs.len)noO} if ( {n);	pblolkXo*=T_agcFSBt);
nytf	p->leng_t	=T0m
	}
r  be16_0noat will bREE_Rf#inade "it nuadexf	by REE_Rf#ii/ */Rde "ode anNwhichfof rbR	filiXETN Itbfigur	stel rwhere/ */Rsu0
	inunfsrly{onsRde "odoM* */pl r* bt(iwat			le.mh of_fileoff_t
REE_Rf#inade "_	c
	uint			ofof#de "o	*ap)	 "bof#ii	de "i	rgumchfoe uint		xf{tr	/ (* RooS_REALTIME_INODE(ap->TSfv&&bap->userrrenB
	x  be16_REE_Rf#inrt	de "_ap), x  be16_REE_Rf#inbt	de "_ap), at will bTrim	ttro  be16xf	m#iient* btrequirxf	b undsmh of_fileofvoidf_t		of#ii_nrim_f#i_	c
	uint			ofofeasie =	*mst ,	c
	uint			ofofeasie =	*S_W,eebp_rfilexntst_	*bXo,e)		*pfilblksllf	len,z)bp_rfilexntst_	obXo,e)		*pfilexntst_	tnq,z)!= XFSX,z)!= XFSflags)f{tr	/ ((flags & * RoBMAPI_ENTIpE	Fel != 0]S_Wblock numbers*+ S_Wblockxfs_bat);
r<_Cobno)*{n);*mst b=T*go
cs; 	/ (,n		wh_rec.xfs_b(S_Wblock numbxfs_b)Xerromst block numbxfs_b}_ DELAYSTARTnt);
;
	x  be16n
 at ;u(blobnoT> *bXo;
		*bXo*=Tobno; x_read(s(*bXo*>_Cobno)*||e(n-
		0)cey _read(s*bXo*<ttnqcey mst block numboxfT= *bno; x	/ (,n		wh_rec.xfs_b(S_Wblock numbxfs_b)Xerrmst block numbxfs_b}_ DELAYSTARTnt);
;
	if (
	Xmst block numbxfs_b}_ S_Wblock numbxfs_b}+ee		r;s*bXo*-]S_Wblock numbers)REeng the R be16_ne b_t({mum Stbwkafowe]S_W_ = 0wkafowe]Rsue	*loM*ftnEXTN	ttroleng_tX  Webcan	u ( * btlen*vari	ble/here/becau ( 
to n the p;difixf	be	xw* = 0w ocmuld havebbeen_ne re/beflengco_t(g the e r/
 f	ne obl= {; umb
nfone oede "od{detdid 't* ts_lapowkaf the wasb	sue	*loMXEXTNUM;mst blockxfs_bat);
r_eturnFILBLKS_MINstnqu- *bXo,e)	XS_Wblockxfs_bat);
r- s*bXo*-]S_Wblock numbers)cey mst block nuheT_eS_Wblock nuhe;oF_read(smst blockxfs_bat);
r<BTven), x  be16, at will bUindir* = 0st idate
ne bwhichfom#iient  be16mh of_fileofvoidf_t		of#ii_ indir_f#i_	c
	uint			ofofeasie =	**map,eebp_rfilexntst_	*bXo,e)		*pfilblksllf	*len,z)bp_rfilexntst_	obXo,e)		*pfilexntst_	tnq,z)!= XFS*X,z)!= XFSflags)f{trbp_releasie = 0;*mst b=T*mapcsn) < mp->(flags & * RoBMAPI_ENTIpE	Fel != 0]]]]l(mst block numboxfT+ mst blockxfs_bat);
)r<BTtnqccey _read(s(flags & * RoBMAPI_ENTIpE	Fel smst blockxfs_bat);
r<BT*lenX el != 0]]]]lmst block numboxfT<Cobno)ceyn)*bXo*=Tmst block numboxfT+ mst blockxfs_bat);
;
;*len BTtnqu- *bXo; x	/ (*n*> 0v&&bmst block numboxfT==Tmst [-1].ock numboxfX = xf		  indiro last entm#iiec_t	(iwainf rmod{det sz);_read(smst block numbxfs_b}_=Tmst [-1].ock numbpSERT)REeF_read(smst blockxfs_bat);
r>Tmst [-1].ockxfs_bat);
	cs) _read(smst block nuhe =_Cmst [-1].ock nuheXLElemst [-1].ockxfs_bat);
*=Tmst blockxfs_bat);
; xfmst [-1].ock nuhe*=Tmst block nuhe;oF} if ( u(bl*n*> 0v&&bmst block numbxfs_b}!_ DELAYSTARTnt);
*&i_bb]]]mst [-1].ock numbpSERT}!_ DELAYSTARTnt);
*&i_bb]]]mst [-1].ock numbpSERT}!_ HOLESTARTnt);
*&i_bb]]]mst block numbxfs_b}_=Tmst [-1].ock numbpSERT}+ee		r; Tmst [-1].ockxfs_bat);
*&i_bb]]]s(flags & * RoBMAPI_IG_filE	Fel !xfmst [-1].ock nuhe*==Tmst block nuhe)f = xf_read(smst block numboxfT== !b]]]]]]]mst [-1].ock numboxfT+ mst [-1].ockxfs_bat);
	cs) mst [-1].ockxfs_bat);
*+=Tmst blockxfs_bat);
; x} if ( u(bl*n*> 0v&&_bb]]]mst block numbxfs_b}_=TDELAYSTARTnt);
*&i_bb]]]mst [-1].ock numbpSERT}_=TDELAYSTARTnt);
*&i_bb]]]mst block numboxfT== !b]]]mst [-1].ock numboxfT+ mst [-1].ockxfs_bat);
	 = xfmst [-1].ockxfs_bat);
*+=Tmst blockxfs_bat);
; xfmst [-1].ock nuhe*=Tmst block nuhe;oF} if ( u(bl!(l*n*
		0)*&i_bb]]]]]l(mst block numboxfT+ mst blockxfs_bat);
)r<B !b]]]]]]obno)c	 = xfmst ++; xfl*n)++; x}
r*map*=Tmst , at will bMap*bllinbfs_bn"entfilesystemnbfs_bn"ec_tel rade "od{de.mh off_t
REE_Rf#ii_			d_	c
	uint			of/* da	*ip,eebp_rfilexntst_	bXo,e)		*pfilblksllf	len,z)
	uint			ofofeasie =	*mst ,	c!= XFS*Xmap,ee!= XFSflags)f{trs_fsblock_t_broof mp*=TTSckin_broonyt
	uint			of/lockr checz)
	uint			ofofeasie =	go
cs;
	uint			ofofeasie =	 las;z)bp_rfilexntst_	obXo;z)bp_rfilexntst_	bnd,OO		*pwhinumllf	lastxey u/* index in  u/* indofn  u/* inn*=T0;
	u/* i	whichlock _ (flags & * RoBMAPI_ATTRmaxr) ?ee		r;	turnATTR maxr*: turnDATA_maxr;Mrn_read(s*Xmap*>_C1cey _read(s (flags & ~(* RoBMAPI_ATTRmaxr|* RoBMAPI_ENTIpEl !xf]]]turnBMAPI_IG_filE	ccey _read(s		of/sifs_bed(TS, * RooLOCK_SHARED|* RooLOCK_EXCL)ceyn)u(blunlikelys* RoTEST_ERROR(_b]]]]pvel)ot levt lMAT(TS(iwhichlock)p!_eturnDINODE_FMT_EXTENTS*&i_b]]]] vel)ot levt lMAT(TS(iwhichlock)p!_eturnDINODE_FMT_BTREEB,ee]]]]]mS, * RoERRTAGnBMAPIt lMAT, * RoRANDOMnBMAPIt lMAT)c	 = xf* RoERROR_REPOd(s"REE_Rf#ii_			d", * RoERRLEVEL_LOW, mpXcs;
r be16_-EFSerror0);
n
 at ;u(blturnFORCED_SHUTDOWN(mp)cs;
r be16_-EIOeyn) = mSfilRo		C(xs
blk_f#irceyn)u(*
	 * Root level mTS(iwhichlock)csn)u(bl!(DR macroflags & * RoIFEXTENTS)X = xfiBMAP_BTREE_i			d		xfs_ino_agc, TS(iwhichlock)cs OBLOCK(bp);
		X  be16_dex inoO} bm_t		of#in->arch		xfs_inoTS(imXo,]whichlock, &exn, &lastx, &S_W,C& las)cs rnqu= bXo*+ len; xobXo*=TbXo;y
swhllinlbXo*<ttnq_li  *<t*XmapX = xf		 R		d{onscter bof,p c/_asNneelgh	ne re'soaohole/uiienttnq., sz);u(blexnXe)	XS_W.ock numboxfTBTtnqcs OBLOCS_W.ock numboxfT> bno)*{n);f		 R		d{ons lpr"holeu 
.t ur,mst block numboxfT= bno; xeXmst block numbxfs_b}_ HOLESTARTnt);
; xeXmst blockxfs_bat);
*=
	r;	turnFILBLKS_MINslen, S_W.ock numboxf*-Tbno);
		 mst block nuheT_e* RoEXT_N lM;
		 bXo*+=bmst blockxfs_bat);
; xf	len*-BTmst blockxfs_bat);
; xf	mst ++; xf	n++; xf	umbeinu	; xf}O xf		 ->iouiiee bwhichfom#iient  be16., sz); = xof#ii_nrim_f#i_mst , &S_W,C&mXo,]len, obXo,Ttnq, n, flags);z); = xof#ii_ indir_f#i_&mst , &bXo,T&len, obXo,Ttnq, &n, flags);z xf		 I*/we'p, dSTa, stop Xew., sz);u(blbXo*>_Ctnq_el n*>_C*XmapX
	leing key xf		 Ef ( go	ST noone oXehi e =ordk, sz);u(bl++lastxp<DDR macro ;
	A /  -
	of(bp_release = 0)	
	lebp_releasgupdate({
		i Nock, i	xf(DR palastx), &S_W	;s Oif (
roriofbBT1; x}
 *Xmap*= n;OO  be16_0noat _fileoff_tf_t		of#ii_		serv
_rr *de "_	c
	uint			of/* da	*ip,eebp_rfilexntst_	aers,er		*pfilblksllf	len,z)
	uint			ofofeasie =	*S_W,ee
	uint			ofofeasie =	* las,z)		*pwhinumllf	*lastx,  u/* indof)f{trs_fsblock_t_broof mp*=TTSckin_broonyt
	uint			of/lockr che
	 * Root level mTS(iturnDATA_maxr);free lwhilen 0;	alenROO		*pwhilen 0; u/dlenROOchar		X t
	 * RooS_REALTIME_INODE(TSf; x		*pwhilen 0; 	xfszey u/* index in 
	alen*=TturnFILBLKS_MINslen, MAX;
}LEN); x	/ (!exnXe)	alen*=TturnFILBLKS_MINsalen, S_Wblock numboxfT- aers)RE
s inFigur	tel r* btt			le  -
	,badjl =*alen*NUM;ix	szp=C		*pk, i	xfsz_hiex_TSf; xu(ble		sz) = xf		 */
		M voisur	twesde 'ttt	c xf/RKs{
	le t			le neng_t	w bnswe */
			dignbW btt			le by e duc{onsleng_t	webmp, go{
		to */
			de "ode by ne omax{mum 	moroooto next -
	t	digmchfom#y
*/
		requirx. */, sz);alen*=TturnFILBLKS_MINslen, MAX;
}LENr- s2he to szp-i1)BREehiBMAP_BTREE_Rf#inrees-
	d	dign(mp,CS_W,C las, to sz, rW,Cbof,ee		r; TTTTTT1, 0, &aers, &alen	;s O_read(s!K(bp);n
 at ;u(bl_e;
		ix	szp=Calen*/nmpblm_sb.sb_rrees-
	; Eeng the M voiaotransacd{de-nessNquotam		servod{detbAP_dr */
	i	de "od{de *r.
xfs_bn. 
Ti n"XFS_BM*k, sbadjl =equloderX  Web  be16_-*/we have 't the ade "ode	*bfs_bn"Rde aaybinsider*  n"loopXEXTNUM;iBMAP_BTREE_trans_		serv
_quota_nbfkno_agc, TS(i(long)alen, 0,erro t
? * RoQMOPT_RES_RTBLKS*: turnQMOPT_RES_REGBLKS)n
lBLOCK(bp);
		  be16_dex ino
lng the Split chang{
		sb f rbRden_anquiedlenbsinc	 ne yocmuld bngco_t(g the fromrqiffirent placenXEXTNUM;
edlenbBT(		*pwhilen 0)REE_Rf#inworsocindlenmTS(ialen	;s _read(s
edlenb>i0BRO ;u(bl_e; = xfiBMAP_BTREE_mod_incore_sb(mS, * RoSBrnFREXTENTS,ee		r; T-(s
et64 0)e		sz)(i0BROO} if ( {n);iBMAP_BTREE_icsb_p;dify_at);
ers(mS, * RoSBrnFDnt);
S,ee		r;	T-(s
et64 0)alen	(i0BROO}

lBLOCK(bp);
		S_WANel _un		serv
_quotaREOOiBMAP_BTREE_icsb_p;dify_at);
ers(mS, * RoSBrnFDnt);
S,ee		r;T-(s
et64 0)
edlen	(i0BROOBLOCK(bp);
		S_WANel _un		serv
_bfs_bn;t ytTSckindr */
	_blks*+=CalenREOOS_Wblock numboxfT= aers;OOS_Wblock numbxfs_b}_ 		wh_rec.xfs_b(
edlen	;OOS_Wblockxfs_bat);
*=TalenROOS_Wblock nuheT_e* RoEXT_N lM;
	REE_Rf#inadd		xfs_i_holendr */mTS(ilastx, S_W	;s
lng the Uindir*t)rowhichfoct xfs_,ugivenbckafoREE_Rf#inadd		xfs_i_holendr */ the pextnuhavebmergod 
to n */on	t f	
	in(iextbtu_{
		on	nXEXTNUM;bp_releasgupdate({
		i Nock, i	xf(DR pa*lastx), S_W	;s
l_read(sS_Wblock numboxfT<= aers)REl_read(sS_Wblock numboxfT+ S_Wblockxfs_bat);
r>= aers +T	den	;s _read(s
n		wh_rec.xfs_b(S_Wblock numbxfs_b)XREl_read(sS_Wblock nuhe*==T* RoEXT_N lMBROO  be16_0no
el _un		serv
_bfs_bn: ;u(bl_e;
		REE_mod_incore_sb(mS, * RoSBrnFREXTENTS, to sz, 0BROOif (
roREE_icsb_p;dify_at);
ers(mS, * RoSBrnFDnt);
S, alen, 0BROel _un		serv
_quota:tr	/ (* RooS_QUOTA_ON(mp)cs;
REE_trans_un		serv
_quota_nbfkno_agc, TS(i(long)alen, 0,0_tn?
	r;	turnQMOPT_RES_RTBLKS*: turnQMOPT_RES_REGBLKS)n
l  be16_dex inoat will bMap*bllinbfs_bn"entfilesystemnbfs_bn,aadding	rr */
	i	de "od{desb	sbl xfe	k,h off_t
REE_Rf#ii_dr */m	c
	uint			of/* da	*ip,X "bincoretnt */
treebp_rfilexntst_	bXo,s in numb{onsbllinntp-.om#ip
	itreebp_rfilblksllf	len,s inveng_t	entmap*insbllintre)e uint			ofofeasie =	*mst , 0};outpl :bmap*st */sotree!= XFS*Xmap,X "bi/o:Tmst t -
	/at);
rtree!= XFSflags)X "bturnBMAPI_...		xf{trs_fsblock_t_broof mp*=TTSckin_broonyt
	uint			of/lockr che
	 * Root level mTS(iturnDATA_maxr);fr
	uint			ofofeasie =	go
cX "bcurrent bllint			le e =ordntre)e uint			ofofeasie =	 las;X "b last entfilint			le e =ordntre)bp_rfilexntst_	obXo; 0};oldrxfs_b}XFS_BM,(ntp->iB tre)bp_rfilexntst_	bnd, 0};tnqunt	m#ip
	ifilinreg{det sz)		*pwhinumllf	lastxes inva =*u (fulnt			le XFS_BM, sz)u/* indofns inwe'vebhi= * bttnqunt		xfs_in, sz)u/* inn*=T0;X "bcurrent t			le 
edex, sz)u/* index i*BTTREOO_read(s*Xmap*>_C1cey _read(s*Xmap*<=eturn_to_cMAX_Nto_cey _read(s (flags & ~* RoBMAPI_ENTIpE	cey _read(s		of/sifs_bed(TS, * RooLOCK_EXCL)ceyn)u(blunlikelys* RoTEST_ERROR(_b]]]]pvel)ot levt lMAT(TS(iturnDATA_maxr)p!_eturnDINODE_FMT_EXTENTS*&i_b]]]] vel)ot levt lMAT(TS(iturnDATA_maxr)p!_eturnDINODE_FMT_BTREEB,ee]]]]]mS, * RoERRTAGnBMAPIt lMAT, * RoRANDOMnBMAPIt lMAT)c	 = xf* RoERROR_REPOd(s"REE_Rf#ii_dr */", * RoERRLEVEL_LOW, mpXcs;
r be16_-EFSerror0);
n
 at ;u(blturnFORCED_SHUTDOWN(mp)cs;
r be16_-EIOeyn) = mSfilRo		C(xs
blk_f#iw)csn)u(bl!(DR macroflags & * RoIFEXTENTS)X = xfiBMAP_BTREE_i			d		xfs_ino_agc, TS(iturnDATA_maxr);frOBLOCK(bp);
		X  be16_dex inoO} bm_t		of#in->arch		xfs_inoTS(imXo,]turnDATA_maxr, &exn, &lastx, &S_W,C& las)cs rnqu= bXo*+ len; xobXo*=TbXo;y
swhllinlbXo*<ttnq_li  *<t*XmapX = xfu(blexn_el S_W.ock numboxfT> bno)*{n);fiBMAP_BTREE_Rf#ii_		serv
_rr *de "_TS(imXo,]len, &S_W,ee	e		r; TT& las, &lastx, exnX; xf	BLOCK(bp); = xf;	u(blX*
		0)*= xf;	 *Xmap*= 0m
		XxX  be16_dex inoO	tf}OO	f;ing key	tf}OO	}O xf		 ->iouiiee bwhichfom#iient  be16., sz); = xof#ii_nrim_f#i_mst , &S_W,C&mXo,]len, obXo,Ttnq, n, flags);z); = xof#ii_ indir_f#i_&mst , &bXo,T&len, obXo,Ttnq, &n, flags);z xf		 I*/we'p, dSTa, stop Xew., sz);u(blbXo*>_Ctnq_el n*>_C*XmapX
	leing key xf		 Ef ( go	ST noone oXehi e =ordk, sz); lasT_eS_W;frOBLOC++lastxp<DDR macro ;
	A /  -
	of(bp_release = 0)	
	lebp_releasgupdate({
		i Nock, i	xf(DR palastx), &S_W	;s Oif (
roriofbBT1; x}

 *Xmap*= n;OO  be16_0noat   nuhic != 
REE_Rf#ii_ade "ode_	c
	uint			ofof#de "o	*of#)f{trs_fsblock_t_broof mp*=Tuma->TSckin_broonytu/* i	whichlock _ (uma->tvags & * RoBMAPI_ATTRmaxr) ?ee		r;	turnATTR maxr*: turnDATA_maxr;Mt
	uint			of/lockr che
	 * Root level muma->TS(iwhichlock)cstu/* i	tmp_logflags BT0;
	u/* index in 
e_read(smma->veng_t	>i0BRO ;ng the F rb* btwasde aya del,0w ocmuld also jl =*ale "ode * btsnuxf*Rsue	 *he fortnt ne n"of#iiuade
butnckafowmuldn'tbbet	sogoodXEXTNUM;
		smma->wasde X*= xfmma->veng_t	BT(		*pwhilen 0)mma->S_W.ockxfs_bat);
; xfmma->ntp->io=Tuma->S_W.ock numboxf;frOBLOCmma->TdxT!=T_agcEXTNUM_li bma->Tdx)*= xf;bp_releasgupdate({
		i Nock, i	xf(DR pamma->TdxT-i1),ee		r;T&uma-> las)cs 	}
	} if ( {
ormma->veng_t	BTturnFILBLKS_MINsmma->veng_t, MAX;
}LEN); x)u(bl!mma->exnXe)	Xmma->veng_t	BTturnFILBLKS_MINsmma->veng_t,ee		r;uma->S_W.ock numboxf*-Tbma->ntp->iBROO}

lng the Indi"ode i>t_i n"is	ne obl= {;user/rren*nt ne nfili,t btjl =*an/ the user/rrenXEXTNUM;
		s!(uma->tvags & * RoBMAPI_METADATA)X*= xfmma->userrren*BT(mma->ntp->io=		0)*?ee		turnALt);_INITIAL_UeadnDATA*: turnALt);_UeadDATAROO}

lmma->minlen*=T(uma->tvags & * RoBMAPI_CONTIG)*?bmma->veng_t	:T1; 
lng the Onlyowa i
tordoone oedignmt	nb = * bttxnbif i  in_userrren* = 
	r.
ede "od{detleng_tiistntrgorNnean	atsnuip	uunitXEXTNUM;
		smpblm_d	dign_li bma->veng_t	>BTmpblm_d	dignT&i_b]]]] (uma->tvags & * RoBMAPI_METADATA)_li whichlock _=iturnDATA_maxr)p= xfiBMAP_BTREE_of#inisaexn(uma(iwhichlock)cs OBLOCK(bp);
		X  be16_dex inoO} bmiBMAP_BTREE_of#in*de "_of#)n
lBLOCK(bp);
		  be16_dex ino
lBLOCmma->tv;	/->xbf_	xwf xfmma->minleft*=T0; xBLOCmma->FS_;
		mma->FS_ the private.b.t = {pSERTb=T*uma->t = {pSERT; xBLOCmma->olkXo*=sb_agcFSBt);
B
	x  be16_TREe	/ ((DR macroflags & * RoIFBROOT)_li !mma->FS_;*= xfmma->FS_F=			ofofeasinit_FS_sor(mS, uma->tp_ouma->TS( whichlock)cs		uma->FS_ the private.b.t = {pSERTb=T*uma->t = {pSERT; x	uma->FS_ the private.b.tv;	/o=Tuma->tv;	/;oO}Eeng the Bumiiee bXFS_BM* Do	xfs_in,we'vebade "ode	 the it ne n"uadeXEXTNUM;uma->nade "s++;  xBLOCmma->FS_;
		mma->FS_ the private.b.tlags Be)	Xmma->wasde 
? * RoBTCUR_BPRV_WASDEL
: 0m

;uma->S_W.ock numboxf*=Tbma->ntp->i;
;uma->S_W.ock numbpSERTb=Tmma->olkXo;
;uma->S_W.ockxfs_bat);
*=Tbma->veng_t;
;uma->S_W.ock nuheT_e* RoEXT_N lM;
Eeng the Atwasde ayawhichfohantheen_initi	dized,
soishmuldn'tbbettlagge	 the as	unwrifdenXEXTNUM;
		s!mma->wasde 
&ibluma->tvags & * RoBMAPI_PREALt);)T&i_b]]]]		ofsb_vers{de_hanwhiflgbit_&mpblm_sb);
		mma->S_W.ock nuheT_e* RoEXT_UNWRITTEN;  xBLOCmma->wasde X xfiBMAP_BTREE_of#in*dd		xfs_i_de ay_			l_of#)n
lif (
roiBMAP_BTREE_of#in*dd		xfs_i_holen			l_of#(iwhichlock)csn)bma->vogflags |= tmp_logflagsn
lBLOCK(bp);
		  be16_dex ino
lng the Uindir*t)rowhichfoct xfs_,ugivenbckafoREE_Rf#inadd		xfs_i_de ay_			l the AP_REE_of#in*dd		xfs_i_holen			l pextnuhavebmergod 
to n */on	t fEXTN	ttro(iextbtu_{
		on	nXEXTNUM;bp_releasgupdate({
		i Nock, i	xf(DR pabma->Tdx),T&uma->S_W	;s
l_read(suma->S_W.ock numboxf*<=Tbma->ntp->iBROO_read(suma->S_W.ock numboxf*+ uma->S_W.ockxfs_bat);
*>= != 0]]]]mma->ntp->io+Tbma->veng_tBROO_read(suma->S_W.ock nuhe*==T* RoEXT_N lM el != 0]]]]uma->S_W.ock nuhe*==T* RoEXT_UNWRITTENBROO  be16_0noat _fileoff_tf_t		of#ii_c nts_e_unwrifden_	c
	uint			ofof#de "o	*of#,z)
	uint			ofofeasie =	*mst ,	c		*pfilblksllf	len,z)!= XFSflags)f{tr	/* i	whichlock _ (flags & * RoBMAPI_ATTRmaxr) ?ee		r;	turnATTR maxr*: turnDATA_maxr;Mt
	uint			of/lockr che
	 * Root level muma->TS(iwhichlock)cstu/* i	tmp_logflags BT0;
	u/* index in 
e/		or nu0-*/we l xf/ */doounwrifden->			l c nts_s{det sz)
		smst block nuhe =_C* RoEXT_UNWRITTENbli_b]]]]ptvags & * RoBMAPI_PREALt);)B
	x  be16_TRE
e/		or nu0-*/we l xf/ */doo			l->unwrifden c nts_s{det sz)
		smst block nuhe =_C* RoEXT_N lM li_b]]]]ptvags & (* RoBMAPI_PREALt); | * RoBMAPI_CONVad())p!_ee		(* RoBMAPI_PREALt); | * RoBMAPI_CONVad())
	x  be16_TRE
e/	 the M;difyOCmyaadding) * btsnutettlag,0-*/wrifingXEXTNUM;_read(smst blockxfs_bat);
r<BTven), x	/ ((DR macroflags & * RoIFBROOT)_li !mma->FS_;*= xfmma->FS_F=			ofofeasinit_FS_sor(uma->TSckin_broo, uma->tp_ee		r;uma->TS( whichlock)cs		uma->FS_ the private.b.t = {pSERTb=T*uma->t = {pSERT; x	uma->FS_ the private.b.tv;	/o=Tuma->tv;	/;oO}Eemst block nuheT_esmst block nuhe =_C* RoEXT_UNWRITTEN	error?C* RoEXT_N lM :e* RoEXT_UNWRITTEN;  xiBMAP_BTREE_of#in*dd		xfs_i_unwrifdenn			l_of#->tp_ouma->TS( &bma->Tdx_ee		&uma->FS_, mst , uma->t = {pSERT,Tuma->tv;	/_ee		&tmp_logflags);n)bma->vogflags |= tmp_logflagsn
lBLOCK(bp);
		  be16_dex ino
lng the Uindir*t)rowhichfoct xfs_,ugivenbckaf the REE_of#in*dd		xfs_i_unwrifdenn			l pextnuhavebmergod 
to n */on	
/
		Stbttro(iextbtu_{
		on	nXEXTNUM;bp_releasgupdate({
		i Nock, i	xf(DR pabma->Tdx),T&uma->S_W	;s
lng the We may havebcombinxf	 last enlyounwrifden space ec_t	wrifden space, *r