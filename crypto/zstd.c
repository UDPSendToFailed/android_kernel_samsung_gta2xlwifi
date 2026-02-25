/*
 * Cryptographic API.
 *
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>

/*
 * Use level 1 (ZSTD_fast strategy) for best throughput on weak cores.
 * Level 1 uses a single hash table lookup vs level 3's double-hash (ZSTD_dfast).
 */
#define ZSTD_DEF_LEVEL	1

struct zstd_ctx {
	ZSTD_CCtx *cctx;
	ZSTD_DCtx *dctx;
	void *cwksp;
	void *dwksp;
};

static ZSTD_parameters zstd_params(void)
{
	return ZSTD_getParams(ZSTD_DEF_LEVEL, PAGE_SIZE, 0);
}

static int zstd_comp_init(struct zstd_ctx *ctx)
{
	int ret = 0;
	const ZSTD_parameters params = zstd_params();
	const size_t wksp_size = zstd_cctx_workspace_bound(&params.cParams);

	ctx->cwksp = vzalloc(wksp_size);
	if (!ctx->cwksp) {
		ret = -ENOMEM;
		goto out;
	}

	ctx->cctx = zstd_init_cctx(ctx->cwksp, wksp_size);
	if (!ctx->cctx) {
		ret = -EINVAL;
		goto out_free;
	}

	/*
	 * Pre-configure the CCtx with all compression parameters once.
	 * On each compress call, we only need a lightweight session reset
	 * instead of re-setting all parameters from scratch.
	 */
	if (ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_compressionLevel, ZSTD_DEF_LEVEL)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_windowLog, params.cParams.windowLog)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_hashLog, params.cParams.hashLog)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_chainLog, params.cParams.chainLog)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_searchLog, params.cParams.searchLog)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_minMatch, params.cParams.minMatch)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_targetLength, params.cParams.targetLength)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_strategy, params.cParams.strategy)) ||
	    /*
	     * Disable content size in frame header — zram already knows
	     * the decompressed size is PAGE_SIZE, so this saves bytes.
	     */
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_contentSizeFlag, 0)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_checksumFlag, 0)) ||
	    ZSTD_isError(ZSTD_CCtx_setParameter(ctx->cctx,
			ZSTD_c_dictIDFlag, 0))) {
		ret = -EINVAL;
		goto out_free;
	}

out:
	return ret;
out_free:
	vfree(ctx->cwksp);
	goto out;
}

static int zstd_decomp_init(struct zstd_ctx *ctx)
{
	int ret = 0;
	const size_t wksp_size = zstd_dctx_workspace_bound();

	ctx->dwksp = vzalloc(wksp_size);
	if (!ctx->dwksp) {
		ret = -ENOMEM;
		goto out;
	}

	ctx->dctx = zstd_init_dctx(ctx->dwksp, wksp_size);
	if (!ctx->dctx) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(ctx->dwksp);
	goto out;
}

static void zstd_comp_exit(struct zstd_ctx *ctx)
{
	vfree(ctx->cwksp);
	ctx->cwksp = NULL;
	ctx->cctx = NULL;
}

static void zstd_decomp_exit(struct zstd_ctx *ctx)
{
	vfree(ctx->dwksp);
	ctx->dwksp = NULL;
	ctx->dctx = NULL;
}

static int __zstd_init(void *ctx)
{
	int ret;

	ret = zstd_comp_init(ctx);
	if (ret)
		return ret;
	ret = zstd_decomp_init(ctx);
	if (ret)
		zstd_comp_exit(ctx);
	return ret;
}

static int zstd_init(struct crypto_tfm *tfm)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zstd_init(ctx);
}

static void __zstd_exit(void *ctx)
{
	zstd_comp_exit(ctx);
	zstd_decomp_exit(ctx);
}

static void zstd_exit(struct crypto_tfm *tfm)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	__zstd_exit(ctx);
}

static int __zstd_compress(const u8 *src, unsigned int slen,
			   u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t out_len;
	struct zstd_ctx *zctx = ctx;

	/*
	 * Session-only reset: preserves all compression parameters that
	 * were configured once at init time. This avoids the overhead of
	 * re-setting windowLog, hashLog, chainLog, searchLog, minMatch,
	 * targetLength, strategy, contentSizeFlag, checksumFlag, and
	 * dictIDFlag on every single 4KB page compress.
	 */
	if (ZSTD_isError(ZSTD_CCtx_reset(zctx->cctx,
					  ZSTD_reset_session_only)))
		return -EINVAL;

	if (ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(zctx->cctx, slen)))
		return -EINVAL;

	out_len = ZSTD_compress2(zctx->cctx, dst, *dlen, src, slen);
	if (ZSTD_isError(out_len))
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int zstd_compress(struct crypto_tfm *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zstd_compress(src, slen, dst, dlen, ctx);
}

static int __zstd_decompress(const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int *dlen, void *ctx)
{
	size_t out_len;
	struct zstd_ctx *zctx = ctx;

	out_len = ZSTD_decompressDCtx(zctx->dctx, dst, *dlen, src, slen);
	if (ZSTD_isError(out_len))
		return -EINVAL;
	*dlen = out_len;
	return 0;
}

static int zstd_decompress(struct crypto_tfm *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct zstd_ctx *ctx = crypto_tfm_ctx(tfm);

	return __zstd_decompress(src, slen, dst, dlen, ctx);
}

static struct crypto_alg alg = {
	.cra_name		= "zstd",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct zstd_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= zstd_init,
	.cra_exit		= zstd_exit,
	.cra_u			= { .compress = {
	.coa_compress		= zstd_compress,
	.coa_decompress		= zstd_decompress } }
};

static int __init zstd_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg);
	if (ret)
		return ret;

	return ret;
}

static void __exit zstd_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(zstd_mod_init);
module_exit(zstd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zstd Compression Algorithm");
MODULE_ALIAS_CRYPTO("zstd");
