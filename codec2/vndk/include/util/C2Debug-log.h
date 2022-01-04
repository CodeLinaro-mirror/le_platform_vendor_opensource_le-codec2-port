/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef C2UTILS_DEBUG_LOG_H_
#define C2UTILS_DEBUG_LOG_H_

// Platform specific debug utilities
#ifdef __ANDROID__
#include <android-base/logging.h>
#include <utils/Log.h>
#include <android-base/stringprintf.h>
using android::base::StringPrintf;

#define C2_LOG(LEVEL) \
        (::android::base::LEVEL != ::android::base::VERBOSE) && LOG(::android::base::LEVEL)
#define C2_CHECK CHECK
#define C2_CHECK_LT CHECK_LT
#define C2_CHECK_LE CHECK_LE
#define C2_CHECK_EQ CHECK_EQ
#define C2_CHECK_GE CHECK_GE
#define C2_CHECK_GT CHECK_GT
#define C2_CHECK_NE CHECK_NE

#define C2_DCHECK DCHECK
#define C2_DCHECK_LT DCHECK_LT
#define C2_DCHECK_LE DCHECK_LE
#define C2_DCHECK_EQ DCHECK_EQ
#define C2_DCHECK_GE DCHECK_GE
#define C2_DCHECK_GT DCHECK_GT
#define C2_DCHECK_NE DCHECK_NE
#else
#include <linux-C2Debug-log.h>
#endif

#endif  // C2UTILS_DEBUG_LOG_H_
