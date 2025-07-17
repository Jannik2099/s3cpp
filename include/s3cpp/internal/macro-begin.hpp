#ifndef S3CPP_MACRO_ACTIVE
#ifndef __clang__
_Pragma("GCC diagnostic push")

    _Pragma("GCC diagnostic ignored_attributes \"clang::lifetimebound\"")
        _Pragma("GCC diagnostic ignored_attributes \"clang::coro_return_type\"")
            _Pragma("GCC diagnostic ignored_attributes \"clang::coro_wrapper\"")
#endif
#define S3CPP_MACRO_ACTIVE
#else
#ifndef S3CPP_CLANG_TIDY
_Pragma("GCC error \"macro header already active\"")
#endif
#endif
