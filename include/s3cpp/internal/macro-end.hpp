#ifdef S3CPP_MACRO_ACTIVE
#ifndef __clang__
_Pragma("GCC diagnostic pop")
#endif
#undef S3CPP_MACRO_ACTIVE
#else
#ifndef S3CPP_CLANG_TIDY
_Pragma("GCC error \"macro header not active\"")
#endif
#endif
