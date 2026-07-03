/*
 * CaraStudio — compatibilité exiv2 0.27 / 0.28
 *
 * exiv2 a modifié son API entre 0.27 et 0.28 :
 *   - Value::toUint32() remplace toLong() ;
 *   - DataBuf expose des accesseurs data()/size() au lieu des membres
 *     publics pData_/size_.
 * exiv2 0.27 est livré par la plupart des distributions stables
 * (Ubuntu 22.04/24.04, Debian 12), 0.28 par les plus récentes
 * (Fedora, Debian 13). Ces helpers, gardés par EXIV2_TEST_VERSION,
 * permettent de compiler CaraStudio sur les deux sans toucher au reste.
 */

#ifndef RS_EXIV2_COMPAT_H
#define RS_EXIV2_COMPAT_H

#include <exiv2/exiv2.hpp>
#include <stdint.h>

static inline uint32_t
rs_exiv_to_uint32(const Exiv2::Value &v)
{
#if EXIV2_TEST_VERSION(0,28,0)
	return v.toUint32();
#else
	return static_cast<uint32_t>(v.toLong());
#endif
}

static inline Exiv2::byte *
rs_exiv_databuf_data(Exiv2::DataBuf &b)
{
#if EXIV2_TEST_VERSION(0,28,0)
	return b.data();
#else
	return b.pData_;
#endif
}

static inline long
rs_exiv_databuf_size(const Exiv2::DataBuf &b)
{
#if EXIV2_TEST_VERSION(0,28,0)
	return static_cast<long>(b.size());
#else
	return b.size_;
#endif
}

#endif /* RS_EXIV2_COMPAT_H */
