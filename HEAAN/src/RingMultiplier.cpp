/*
* Copyright (c) by CryptoLab inc.
* This program is licensed under a
* Creative Commons Attribution-NonCommercial 3.0 Unported License.
* You should have received a copy of the license along with this
* work.  If not, see <http://creativecommons.org/licenses/by-nc/3.0/>.
*/

#include "RingMultiplier.h"

#include <NTL/BasicThreadPool.h>
#include <NTL/lip.h>
#include <NTL/sp_arith.h>
#include <NTL/tools.h>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

#include "Primes.h"

RingMultiplier::RingMultiplier(long logN, long logQ) : logN(logN) {
	N = 1 << logN;
	long M = N << 1;

	long bound = 2 + logN + 4 * logQ;
	long nprimes = ceil(bound / 59.0);

	pVec = new uint64_t[nprimes];
	prVec = new uint64_t[nprimes];
	pTwok = new long[nprimes];
	pInvVec = new uint64_t[nprimes];
	scaledRootPows = new uint64_t*[nprimes];
	scaledRootInvPows = new uint64_t*[nprimes];
	scaledNInv = new uint64_t[nprimes];

	red_ss_array = new _ntl_general_rem_one_struct*[nprimes];

	for (long i = 0; i < nprimes; ++i) {
		pVec[i] = pPrimesVec[i];
		red_ss_array[i] = _ntl_general_rem_one_struct_build(pVec[i]);
		pInvVec[i] = inv(pVec[i]);
		pTwok[i] = (2 * ((long) log2(pVec[i]) + 1));
		prVec[i] = (static_cast<unsigned __int128>(1) << pTwok[i]) / pVec[i];
		uint64_t root = findMthRootOfUnity(M, pVec[i]);
		uint64_t rootinv = invMod(root, pVec[i]);
		uint64_t NInv = invMod(N, pVec[i]);
		mulMod(scaledNInv[i], NInv, (1ULL << 32), pVec[i]);
		mulMod(scaledNInv[i], scaledNInv[i], (1ULL << 32), pVec[i]);
		scaledRootPows[i] = new uint64_t[N]();
		scaledRootInvPows[i] = new uint64_t[N]();
		uint64_t power = 1;
		uint64_t powerInv = 1;
		for (long j = 0; j < N; ++j) {
			uint32_t jprime = bitReverse(static_cast<uint32_t>(j)) >> (32 - logN);
			uint64_t rootpow = power;
			mulMod(scaledRootPows[i][jprime], rootpow,(1ULL << 32), pVec[i]);
			mulMod(scaledRootPows[i][jprime], scaledRootPows[i][jprime], (1ULL << 32), pVec[i]);
			uint64_t rootpowInv = powerInv;
			mulMod(scaledRootInvPows[i][jprime], rootpowInv, (1ULL << 32), pVec[i]);
			mulMod(scaledRootInvPows[i][jprime], scaledRootInvPows[i][jprime], (1ULL << 32), pVec[i]);
			mulMod(power, power, root, pVec[i]);
			mulMod(powerInv, powerInv, rootinv, pVec[i]);
		}
	}

	coeffpinv_array = new mulmod_precon_t*[nprimes];
	pProd = new ZZ[nprimes];
	pProdh = new ZZ[nprimes];
	pHat = new ZZ*[nprimes];
	pHatInvModp = new uint64_t*[nprimes];
	for (long i = 0; i < nprimes; ++i) {
		pProd[i] = (i == 0) ? to_ZZ((long) pVec[i]) : pProd[i - 1] * (long) pVec[i];
		pProdh[i] = pProd[i] / 2;
		pHat[i] = new ZZ[i + 1];
		pHatInvModp[i] = new uint64_t[i + 1];
		coeffpinv_array[i] = new mulmod_precon_t[i + 1];
		for (long j = 0; j < i + 1; ++j) {
			pHat[i][j] = ZZ(1);
			for (long k = 0; k < j; ++k) {
				pHat[i][j] *= (long) pVec[k];
			}
			for (long k = j + 1; k < i + 1; ++k) {
				pHat[i][j] *= (long) pVec[k];
			}
			pHatInvModp[i][j] = to_long(pHat[i][j] % (long) pVec[j]);
			pHatInvModp[i][j] = invMod(pHatInvModp[i][j], pVec[j]);
			coeffpinv_array[i][j] = PrepMulModPrecon(pHatInvModp[i][j], pVec[j]);
		}
	}
}

void RingMultiplier::NTT(uint64_t* a, long index) {
	long t = N;
	long logt1 = logN + 1;
	uint64_t p = pVec[index];
	uint64_t pInv = pInvVec[index];
	for (long m = 1; m < N; m <<= 1) {
		t >>= 1;
		logt1 -= 1;
		for (long i = 0; i < m; i++) {
			long j1 = i << logt1;
			long j2 = j1 + t - 1;
			uint64_t W = scaledRootPows[index][m + i];
			for (long j = j1; j <= j2; j++) {
				uint64_t T = a[j + t];
				unsigned __int128
				U = static_cast<unsigned __int128>(T) * W;
				uint64_t U0 = static_cast<uint64_t>(U);
				uint64_t U1 = U >> 64;
				uint64_t Q = U0 * pInv;
				unsigned __int128
				Hx = static_cast<unsigned __int128>(Q) * p;
				uint64_t H = Hx >> 64;
				uint64_t V = U1 < H ? U1 + p - H : U1 - H;
				a[j + t] = a[j] < V ? a[j] + p - V : a[j] - V;
				a[j] += V;
				if (a[j] > p)
					a[j] -= p;
			}
		}
	}
}

void RingMultiplier::INTT(uint64_t* a, long index) {
	uint64_t p = pVec[index];
	uint64_t pInv = pInvVec[index];
	long t = 1;
	for (long m = N; m > 1; m >>= 1) {
		long j1 = 0;
		long h = m >> 1;
		for (long i = 0; i < h; i++) {
			long j2 = j1 + t - 1;
			uint64_t W = scaledRootInvPows[index][h + i];
			for (long j = j1; j <= j2; j++) {
				uint64_t U = a[j] + a[j + t];
				if (U > p)
					U -= p;
				uint64_t T =
						a[j] < a[j + t] ? a[j] + p - a[j + t] : a[j] - a[j + t];
				unsigned __int128
				UU = static_cast<unsigned __int128>(T) * W;
				uint64_t U0 = static_cast<uint64_t>(UU);
				uint64_t U1 = UU >> 64;
				uint64_t Q = U0 * pInv;
				unsigned __int128
				Hx = static_cast<unsigned __int128>(Q) * p;
				uint64_t H = Hx >> 64;
				a[j] = U;
				a[j + t] = (U1 < H) ? U1 + p - H : U1 - H;
			}
			j1 += (t << 1);
		}
		t <<= 1;
	}

	uint64_t NScale = scaledNInv[index];
	for (long i = 0; i < N; i++) {
		uint64_t T = a[i];
		unsigned __int128
		U = static_cast<unsigned __int128>(T) * NScale;
		uint64_t U0 = static_cast<uint64_t>(U);
		uint64_t U1 = U >> 64;
		uint64_t Q = U0 * pInv;
		unsigned __int128
		Hx = static_cast<unsigned __int128>(Q) * p;
		uint64_t H = Hx >> 64;
		a[i] = (U1 < H) ? U1 + p - H : U1 - H;
	}
}

//----------------------------------------------------------------------------------
//   FFT
//----------------------------------------------------------------------------------

uint64_t* RingMultiplier::toNTT(ZZ* x, long np) {
	uint64_t* rx = new uint64_t[np << logN]();
	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rxi = rx + (i << logN);
		uint64_t pi = pVec[i];
		uint64_t pri = prVec[i];
		long pTwoki = pTwok[i];
		_ntl_general_rem_one_struct* red_ss = red_ss_array[i];
		for (long n = 0; n < N; ++n) {
			rxi[n] = _ntl_general_rem_one_struct_apply(x[n].rep, pi, red_ss);
		}
		NTT(rxi, i);
	}
	NTL_EXEC_RANGE_END;
	return rx;
}

uint64_t* RingMultiplier::addNTT(uint64_t* ra, uint64_t* rb, long np) {
	uint64_t* res = new uint64_t[np << logN];
	for (long i = 0; i < np; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rbi = rb + (i << logN);
		uint64_t* resi = res + (i << logN);
		uint64_t pi = pVec[i];
		for (long n = 0; n < N; ++n) {
			resi[n] = rai[n] + rbi[n];
			if(resi[n] > pi) resi[n] -= pi;
		}
	}
	return res;
}

void RingMultiplier::reconstruct(ZZ* x, uint64_t* rx, long np, ZZ& mod) {
	ZZ* pHatnp = pHat[np - 1];
	uint64_t* pHatInvModpnp = pHatInvModp[np - 1];
	mulmod_precon_t* coeffpinv_arraynp = coeffpinv_array[np - 1];
	ZZ& pProdnp = pProd[np - 1];
	ZZ& pProdhnp = pProdh[np - 1];

	NTL_EXEC_RANGE(N, first, last);
	for (long n = first; n < last; ++n) {
		ZZ& acc = x[n];
		QuickAccumBegin(acc, pProdnp.size());
		for (long i = 0; i < np; i++) {
			long p = pVec[i];
			long tt = pHatInvModpnp[i];
			mulmod_precon_t ttpinv = coeffpinv_arraynp[i];
			long s = MulModPrecon(rx[n + (i << logN)], tt, p, ttpinv);
			QuickAccumMulAdd(acc, pHatnp[i], s);
		}
		QuickAccumEnd(acc);
		QuickRem(x[n], pProdnp);
		if (x[n] > pProdhnp) x[n] -= pProdnp;
		QuickRem(x[n], mod);
	}
	NTL_EXEC_RANGE_END;

}

void RingMultiplier::mult(ZZ* x, ZZ* a, ZZ* b, long np, ZZ& mod) {
	uint64_t* ra = new uint64_t[np << logN]();
	uint64_t* rb = new uint64_t[np << logN]();
	uint64_t* rx = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rbi = rb + (i << logN);
		uint64_t* rxi = rx + (i << logN);
		uint64_t pi = pVec[i];
		uint64_t pri = prVec[i];
		long pTwoki = pTwok[i];
		_ntl_general_rem_one_struct* red_ss = red_ss_array[i];
		for (long n = 0; n < N; ++n) {
			rai[n] = _ntl_general_rem_one_struct_apply(a[n].rep, pi, red_ss);
			rbi[n] = _ntl_general_rem_one_struct_apply(b[n].rep, pi, red_ss);
		}
		NTT(rai, i);
		NTT(rbi, i);
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rxi[n], rai[n], rbi[n], pi, pri, pTwoki);
		}
		INTT(rxi, i);
	}
	NTL_EXEC_RANGE_END;

	reconstruct(x, rx, np, mod);

	delete[] ra;
	delete[] rb;
	delete[] rx;
}

void RingMultiplier::multNTT(ZZ* x, ZZ* a, uint64_t* rb, long np, ZZ& mod) {
	uint64_t* ra = new uint64_t[np << logN]();
	uint64_t* rx = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rbi = rb + (i << logN);
		uint64_t* rxi = rx + (i << logN);
		uint64_t pi = pVec[i];
		uint64_t pri = prVec[i];
		long pTwoki = pTwok[i];
		_ntl_general_rem_one_struct* red_ss = red_ss_array[i];
		for (long n = 0; n < N; ++n) {
			rai[n] = _ntl_general_rem_one_struct_apply(a[n].rep, pi, red_ss);
		}
		NTT(rai, i);
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rxi[n], rai[n], rbi[n], pi, pri, pTwoki);
		}
		INTT(rxi, i);
	}
	NTL_EXEC_RANGE_END;

	reconstruct(x, rx, np, mod);

	delete[] ra;
	delete[] rx;
}

void RingMultiplier::multDNTT(ZZ* x, uint64_t* ra, uint64_t* rb, long np, ZZ& mod) {
	uint64_t* rx = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rbi = rb + (i << logN);
		uint64_t* rxi = rx + (i << logN);
		uint64_t pi = pVec[i];
		uint64_t pri = prVec[i];
		long pTwoki = pTwok[i];
		_ntl_general_rem_one_struct* red_ss = red_ss_array[i];
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rxi[n], rai[n], rbi[n], pi, pri, pTwoki);
		}
		INTT(rxi, i);
	}
	NTL_EXEC_RANGE_END;

	reconstruct(x, rx, np, mod);

	delete[] rx;
}

void RingMultiplier::multAndEqual(ZZ* a, ZZ* b, long np, ZZ& mod) {
	uint64_t* ra = new uint64_t[np << logN]();
	uint64_t* rb = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rbi = rb + (i << logN);
		uint64_t pi = pVec[i];
		for (long n = 0; n < N; ++n) {
			rai[n] = _ntl_general_rem_one_struct_apply(a[n].rep, pi, red_ss_array[i]);
			rbi[n] = _ntl_general_rem_one_struct_apply(b[n].rep, pi, red_ss_array[i]);
		}
		NTT(rai, i);
		NTT(rbi, i);
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rai[n], rai[n], rbi[n], pi, prVec[i], pTwok[i]);
		}
		INTT(rai, i);
	}
	NTL_EXEC_RANGE_END;

	ZZ* pHatnp = pHat[np - 1];
	uint64_t* pHatInvModpnp = pHatInvModp[np - 1];

	reconstruct(a, ra, np, mod);

	delete[] ra;
	delete[] rb;
}

void RingMultiplier::multNTTAndEqual(ZZ* a, uint64_t* rb, long np, ZZ& mod) {
	uint64_t* ra = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rbi = rb + (i << logN);
		uint64_t pi = pVec[i];
		for (long n = 0; n < N; ++n) {
			rai[n] = _ntl_general_rem_one_struct_apply(a[n].rep, pi, red_ss_array[i]);
		}
		NTT(rai, i);
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rai[n], rai[n], rbi[n], pi, prVec[i], pTwok[i]);
		}
		INTT(rai, i);
	}
	NTL_EXEC_RANGE_END;

	ZZ* pHatnp = pHat[np - 1];
	uint64_t* pHatInvModpnp = pHatInvModp[np - 1];

	reconstruct(a, ra, np, mod);

	delete[] ra;
}


void RingMultiplier::square(ZZ* x, ZZ* a, long np, ZZ& mod) {
	uint64_t* ra = new uint64_t[np << logN]();
	uint64_t* rx = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rxi = rx + (i << logN);
		uint64_t pi = pVec[i];
		for (long n = 0; n < N; ++n) {
			rai[n] = _ntl_general_rem_one_struct_apply(a[n].rep, pi, red_ss_array[i]);
		}
		NTT(rai, i);
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rxi[n], rai[n], rai[n], pi, prVec[i], pTwok[i]);
		}
		INTT(rxi, i);
	}
	NTL_EXEC_RANGE_END;

	ZZ* pHatnp = pHat[np - 1];
	uint64_t* pHatInvModpnp = pHatInvModp[np - 1];

	reconstruct(x, rx, np, mod);

	delete[] ra;
	delete[] rx;
}

void RingMultiplier::squareNTT(ZZ* x, uint64_t* ra, long np, ZZ& mod) {
	uint64_t* rx = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t* rxi = rx + (i << logN);
		uint64_t pi = pVec[i];
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rxi[n], rai[n], rai[n], pi, prVec[i], pTwok[i]);
		}
		INTT(rxi, i);
	}
	NTL_EXEC_RANGE_END;

	reconstruct(x, rx, np, mod);

	delete[] rx;
}

void RingMultiplier::squareAndEqual(ZZ* a, long np, ZZ& mod) {
	uint64_t* ra = new uint64_t[np << logN]();

	NTL_EXEC_RANGE(np, first, last);
	for (long i = first; i < last; ++i) {
		uint64_t* rai = ra + (i << logN);
		uint64_t pi = pVec[i];
		for (long n = 0; n < N; ++n) {
			rai[n] = _ntl_general_rem_one_struct_apply(a[n].rep, pi, red_ss_array[i]);
		}
		NTT(rai, i);
		for (long n = 0; n < N; ++n) {
			mulModBarrett(rai[n], rai[n], rai[n], pi, prVec[i], pTwok[i]);
		}
		INTT(rai, i);
	}
	NTL_EXEC_RANGE_END;

	reconstruct(a, ra, np, mod);

	delete[] ra;
}

void RingMultiplier::mulMod(uint64_t &r, uint64_t a, uint64_t b, uint64_t m) {
	unsigned __int128 mul = static_cast<unsigned __int128>(a) * b;
	mul %= static_cast<unsigned __int128>(m);
	r = static_cast<uint64_t>(mul);
}

void RingMultiplier::mulModBarrett(uint64_t& r, uint64_t a, uint64_t b, uint64_t p, uint64_t pr, long twok) {
	unsigned __int128 mul = static_cast<unsigned __int128>(a) * b;
	uint64_t atop, abot;
	abot = static_cast<uint64_t>(mul);
	atop = static_cast<uint64_t>(mul >> 64);
	unsigned __int128 tmp = static_cast<unsigned __int128>(abot) * pr;
	tmp >>= 64;
	tmp += static_cast<unsigned __int128>(atop) * pr;
	tmp >>= twok - 64;
	tmp *= p;
	tmp = mul - tmp;
	r = static_cast<uint64_t>(tmp);
	if(r >= p) {
		r -= p;
	}
}

uint64_t RingMultiplier::invMod(uint64_t x, uint64_t m) {
	return powMod(x, m - 2, m);
}

uint64_t RingMultiplier::powMod(uint64_t x, uint64_t y, uint64_t modulus) {
	uint64_t res = 1;
	while (y > 0) {
		if (y & 1) {
			mulMod(res, res, x, modulus);
		}
		y = y >> 1;
		mulMod(x, x, x, modulus);
	}
	return res;
}

uint64_t RingMultiplier::inv(uint64_t x) {
	return pow(x, static_cast<uint64_t>(-1));
}

uint64_t RingMultiplier::pow(uint64_t x, uint64_t y) {
	uint64_t res = 1;
	while (y > 0) {
		if (y & 1) {
			res *= x;
		}
		y = y >> 1;
		x *= x;
	}
	return res;
}

uint32_t RingMultiplier::bitReverse(uint32_t x) {
	x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
	x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
	x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
	x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
	return ((x >> 16) | (x << 16));
}

void RingMultiplier::findPrimeFactors(vector<uint64_t> &s, uint64_t number) {
	while (number % 2 == 0) {
		s.push_back(2);
		number /= 2;
	}
	for (uint64_t i = 3; i < sqrt(number); i++) {
		while (number % i == 0) {
			s.push_back(i);
			number /= i;
		}
	}
	if (number > 2) {
		s.push_back(number);
	}
}

uint64_t RingMultiplier::findPrimitiveRoot(uint64_t modulus) {
	vector<uint64_t> s;
	uint64_t phi = modulus - 1;
	findPrimeFactors(s, phi);
	for (uint64_t r = 2; r <= phi; r++) {
		bool flag = false;
		for (auto it = s.begin(); it != s.end(); it++) {
			if (powMod(r, phi / (*it), modulus) == 1) {
				flag = true;
				break;
			}
		}
		if (flag == false) {
			return r;
		}
	}
	return -1;
}

// Algorithm to find m-th primitive root in Z_mod
uint64_t RingMultiplier::findMthRootOfUnity(uint64_t M, uint64_t mod) {
    uint64_t res;
    res = findPrimitiveRoot(mod);
    if((mod - 1) % M == 0) {
        uint64_t factor = (mod - 1) / M;
        res = powMod(res, factor, mod);
        return res;
    }
    else {
        return -1;
    }
}


