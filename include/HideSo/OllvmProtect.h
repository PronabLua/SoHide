#pragma once

#if defined(OG_OLLVM_ENABLED) && defined(__clang__)
#pragma clang attribute push(__attribute__((annotate("indbr"))), apply_to=function)
#endif
