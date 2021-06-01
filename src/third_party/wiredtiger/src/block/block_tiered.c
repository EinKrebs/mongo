/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_block_tiered_flush --
 *     Flush this file, start another file.
 */
int
__wt_block_tiered_flush(
  WT_SESSION_IMPL *session, WT_BLOCK *block, uint8_t **flush_cookie, size_t *cookie_size)
{
    /* TODO: tiered: fill in the cookie. */
    (void)flush_cookie;
    (void)cookie_size;

    return (__wt_block_tiered_newfile(session, block));
}

/*
 * __wt_block_tiered_load --
 *     Set up log-structured processing when loading a new root page.
 */
int
__wt_block_tiered_load(WT_SESSION_IMPL *session, WT_BLOCK *block, WT_BLOCK_CKPT *ci)
{
    /*
     * TODO: tiered: this call currently advances the object id, that's probably not appropriate for
     * readonly opens. Perhaps it's also not appropriate for opening at an older checkpoint?
     */
    if (block->has_objects) {
        block->objectid = ci->root_objectid;

        /* Advance to the next file for future changes. */
        WT_RET(__wt_block_tiered_newfile(session, block));
    }
    return (0);
}

/*
 * __wt_block_tiered_newfile --
 *     Switch a log-structured block object to a new file.
 */
int
__wt_block_tiered_newfile(WT_SESSION_IMPL *session, WT_BLOCK *block)
{
    WT_DECL_ITEM(tmp);
    WT_DECL_RET;
    const char *filename;

    /* Get the old file name again. */
    WT_ERR(__wt_scr_alloc(session, 0, &tmp));

    /*
     * TODO: tiered: We will get rid of the log id, and this name generation will be replaced by the
     * name generated by __tiered_switch.
     */
    WT_ERR(__wt_close(session, &block->fh));

    /* Bump to a new file ID. */
    ++block->objectid;
    WT_ERR(__wt_buf_fmt(session, tmp, "%s.%08" PRIu32, block->name, block->objectid));
    filename = tmp->data;

    WT_WITH_BUCKET_STORAGE(session->bucket_storage, session, {
        ret = __wt_open(session, filename, WT_FS_OPEN_FILE_TYPE_DATA,
          WT_FS_OPEN_CREATE | block->file_flags, &block->fh);
    });
    WT_ERR(ret);
    WT_ERR(__wt_desc_write(session, block->fh, block->allocsize));

    block->size = block->allocsize;
    __wt_block_ckpt_destroy(session, &block->live);
    WT_ERR(__wt_block_ckpt_init(session, &block->live, "live"));

err:
    __wt_scr_free(session, &tmp);
    return (ret);
}