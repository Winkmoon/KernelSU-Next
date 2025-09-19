#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>
#ifdef CONFIG_KSU_DEBUG
#include <linux/moduleparam.h>
#endif
#include <crypto/hash.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#include <crypto/sha2.h>
#else
#include <crypto/sha.h>
#endif
#include "apk_sign.h"
#include "klog.h" // IWYU pragma: keep
#include "kernel_compat.h"
#include "throne_tracker.h"
struct manager_signature {
    unsigned int expected_size;
    const char *expected_hash;
};

static struct manager_signature manager_signatures[] = {
    {0x033b, "c371061b19d8c7d7d6133c6a9bafe198fa944e50c1b31c9d8daa8d7f1fc2d2d6"}, // tiann/KernelSU
    {384,   "7e0c6d7278a3bb8e364e0fcba95afaf3666cf5ff3c245a3b63c8833bd0445cc4"}, // 5ec1cff/KernelSU
    {0x3e6, "79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7"}, // rifsxd/KernelSU-Next
    {0x396, "f415f4ed9435427e1fdf7f1fccd4dbc07b3d6b8751e4dbcec6f19671f427870b"}, // rsuntk/KernelSU
    {0x363, "4359c171f32543394cbc23ef908c4bb94cad7c8087002ba164c8230948c21549"}, // backslashxx/KernelSU
    {0x35c, "947ae944f3de4ed4c21a7e4f7953ecf351bfa2b36239da37a34111ad29993eef"}, // ShirkNeko/SukiSU-Ultra
};

struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
	struct sdesc *sdesc;
	int size;

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return ERR_PTR(-ENOMEM);
	sdesc->shash.tfm = alg;
	return sdesc;
}

static int calc_hash(struct crypto_shash *alg, const unsigned char *data,
		     unsigned int datalen, unsigned char *digest)
{
	struct sdesc *sdesc;
	int ret;

	sdesc = init_sdesc(alg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc sdesc\n");
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
	kfree(sdesc);
	return ret;
}

static int ksu_sha256(const unsigned char *data, unsigned int datalen,
		      unsigned char *digest)
{
	struct crypto_shash *alg;
	char *hash_alg_name = "sha256";
	int ret;

	alg = crypto_alloc_shash(hash_alg_name, 0, 0);
	if (IS_ERR(alg)) {
		pr_info("can't alloc alg %s\n", hash_alg_name);
		return PTR_ERR(alg);
	}
	ret = calc_hash(alg, data, datalen, digest);
	crypto_free_shash(alg);
	return ret;
}

static bool check_block(struct file *fp, u32 *size4, loff_t *pos, u32 *offset,
			unsigned expected_size, const char *expected_sha256)
{
	ksu_kernel_read_compat(fp, size4, 0x4, pos); // signer-sequence length
	ksu_kernel_read_compat(fp, size4, 0x4, pos); // signer length
	ksu_kernel_read_compat(fp, size4, 0x4, pos); // signed data length

	*offset += 0x4 * 3;

	ksu_kernel_read_compat(fp, size4, 0x4, pos); // digests-sequence length

	*pos += *size4;
	*offset += 0x4 + *size4;

	ksu_kernel_read_compat(fp, size4, 0x4, pos); // certificates length
	ksu_kernel_read_compat(fp, size4, 0x4, pos); // certificate length
	*offset += 0x4 * 2;

	if (*size4 == expected_size) {
		*offset += *size4;

#define CERT_MAX_LENGTH 1024
		char cert[CERT_MAX_LENGTH];
		if (*size4 > CERT_MAX_LENGTH) {
			pr_info("cert length overlimit\n");
			return false;
		}
		ksu_kernel_read_compat(fp, cert, *size4, pos);
		unsigned char digest[SHA256_DIGEST_SIZE];
		if (IS_ERR(ksu_sha256(cert, *size4, digest))) {
			pr_info("sha256 error\n");
			return false;
		}

		char hash_str[SHA256_DIGEST_SIZE * 2 + 1];
		hash_str[SHA256_DIGEST_SIZE * 2] = '\0';

		bin2hex(hash_str, digest, SHA256_DIGEST_SIZE);
		pr_info("sha256: %s, expected: %s\n", hash_str,
			expected_sha256);
		if (strcmp(expected_sha256, hash_str) == 0) {
			return true;
		}
	}
	return false;
}

bool is_manager_apk(char *path)
{
	int tries = 0;

	while (tries++ < 10) {
		if (!is_lock_held(path))
			break;

		pr_info("%s: waiting for %s\n", __func__, path);
		msleep(100);
	}

	if (tries == 10) {
		pr_info("%s: timeout for %s\n", __func__, path);
		return false;
	}

	pr_info("%s: checking against multiple manager signatures...\n", path);

	for (int i = 0; i < ARRAY_SIZE(manager_signatures); i++) {
		if (check_v2_signature(path,
				       manager_signatures[i].expected_size,
				       manager_signatures[i].expected_hash)) {
			pr_info("%s: matched manager signature index %d\n", path, i);
			return true;
		}
	}

	pr_info("%s: no matching manager signature found\n", path);
	return false;
}
