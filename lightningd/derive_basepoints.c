#include <assert.h>
#include <ccan/crypto/hkdf_sha256/hkdf_sha256.h>
#include <ccan/crypto/sha256/sha256.h>
#include <ccan/crypto/shachain/shachain.h>
#include <lightningd/derive_basepoints.h>
#include <utils.h>

bool derive_basepoints(const struct privkey *seed,
		       struct pubkey *funding_pubkey,
		       struct basepoints *basepoints,
		       struct secrets *secrets,
		       struct sha256 *shaseed,
		       struct pubkey *per_commit_point,
		       u64 per_commit_index)
{
	struct sha256 per_commit_secret;
	struct keys {
		struct privkey f, r, p, d;
		struct sha256 shaseed;
	} keys;

	hkdf_sha256(&keys, sizeof(keys), NULL, 0, seed, sizeof(*seed),
		    "c-lightning", strlen("c-lightning"));

	secrets->funding_privkey = keys.f;
	secrets->revocation_basepoint_secret = keys.r;
	secrets->payment_basepoint_secret = keys.p;
	secrets->delayed_payment_basepoint_secret = keys.d;

	if (!pubkey_from_privkey(&keys.f, funding_pubkey)
	    || !pubkey_from_privkey(&keys.r, &basepoints->revocation)
	    || !pubkey_from_privkey(&keys.p, &basepoints->payment)
	    || !pubkey_from_privkey(&keys.d, &basepoints->delayed_payment))
		return false;

	/* BOLT #3:
	 *
	 * A node MUST select an unguessable 256-bit seed for each connection,
	 * and MUST NOT reveal the seed.
	 */
	*shaseed = keys.shaseed;

	shachain_from_seed(shaseed, shachain_index(per_commit_index),
			   &per_commit_secret);

	/* BOLT #3:
	 *
	 * The `per-commitment-point` is generated using EC multiplication:
	 *
	 * 	per-commitment-point = per-commitment-secret * G
	 */
	if (secp256k1_ec_pubkey_create(secp256k1_ctx,
				       &per_commit_point->pubkey,
				       per_commit_secret.u.u8) != 1)
		return false;

	return true;
}

bool next_per_commit_point(const struct sha256 *shaseed,
			   struct sha256 *old_commit_secret,
			   struct pubkey *per_commit_point,
			   u64 per_commit_index)
{
	struct sha256 per_commit_secret;


	/* Get old secret. */
	if (per_commit_index > 0)
		shachain_from_seed(shaseed, shachain_index(per_commit_index - 1),
				   old_commit_secret);
	else
		assert(old_commit_secret == NULL);

	/* Derive new per-commitment-point. */
	shachain_from_seed(shaseed, shachain_index(per_commit_index + 1),
			   &per_commit_secret);

	/* BOLT #3:
	 *
	 * The `per-commitment-point` is generated using EC multiplication:
	 *
	 * 	per-commitment-point = per-commitment-secret * G
	 */
	if (secp256k1_ec_pubkey_create(secp256k1_ctx,
				       &per_commit_point->pubkey,
				       per_commit_secret.u.u8) != 1)
		return false;

	return true;
}
