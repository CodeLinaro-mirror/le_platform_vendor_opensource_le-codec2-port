/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (C) 2005-2014 The Android Open Source Project
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef C2_LINUX_DEBUG_LOG_H_
#define C2_LINUX_DEBUG_LOG_H_

#include <utils/Log.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <cstring>
#include <limits.h>
#include <cutils/memory.h>

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#define PAGE_SIZE  4096
#define LOG_NDEBUG 0

extern uint32_t gCodec2LogLevel;

// Placeholder stringprintf stub
// implent the correct function
#define StringPrintf(fmt, ...) " "

#define C2_CHECK
#define C2_CHECK_LT
#define C2_CHECK_LE
#define C2_CHECK_EQ
#define C2_CHECK_GE
#define C2_CHECK_GT
#define C2_CHECK_NE

#define C2_DCHECK
#define C2_DCHECK_LT
#define C2_DCHECK_LE
#define C2_DCHECK_EQ
#define C2_DCHECK_GE
#define C2_DCHECK_GT
#define C2_DCHECK_NE

#define LOG_LEVEL(level) std::cout.setstate(std::ios::failbit) ;\
                                std::cout
#define LOG(LEVEL) LOG_LEVEL(logLevel::LEVEL)
#define C2_LOG(LEVEL) LOG_LEVEL(logLevel::LEVEL)

/** Corresponds to the Android ERROR log priority. */
#define LOG_EMERG 0
/** Corresponds to the Android ERROR log priority. */
#define LOG_ALERT 1
/** Corresponds to the Android ERROR log priority. */
#define LOG_CRIT 2
/** Corresponds to the Android ERROR log priority. */
#define LOG_ERR 3
/** Corresponds to the Android WARN log priority. */
#define LOG_WARNING 4
/** Corresponds to the Android INFO log priority. */
#define LOG_NOTICE 5
/** Corresponds to the Android INFO log priority. */
#define LOG_INFO 6
/** Corresponds to the Android DEBUG log priority. */
#define LOG_DEBUG 7

//#define ALOG(level, tag, ...) printf(tag, __VA_ARGS__)
/*
 * Normally we strip ALOGV (VERBOSE messages) from release builds.
 * You can modify this (for example with "#define LOG_NDEBUG 0"
 * at the top of your source file) to change that behavior.
 */
#define LOG_NDEBUG 1
#ifndef LOG_NDEBUG
#ifdef NDEBUG
#define LOG_NDEBUG 1
#else
#define LOG_NDEBUG 0
#endif
#endif

/*
 * This is the local tag used for the following simplified
 * logging macros.  You can change this preprocessor definition
 * before using the other macros to change the tag.
 */
#ifndef LOG_TAG
#define LOG_TAG "Codec2VndkLe"
#endif

enum C2LogLevel : uint32_t {
    C2_MSGLEVEL_INFO = 0x01,
    C2_MSGLEVEL_DEBUG = 0x02,
    C2_MSGLEVEL_VERBOSE = 0x4,
};

#define ALOGV(format, args...) ALOGV_IF((gCodec2LogLevel & C2_MSGLEVEL_VERBOSE), format, ##args)
#define ALOGD(format, args...) ALOGD_IF((gCodec2LogLevel & C2_MSGLEVEL_DEBUG), format, ##args)
#define ALOGI(format, args...) ALOGI_IF((gCodec2LogLevel & C2_MSGLEVEL_INFO), format, ##args)

#endif  // C2_HOST_DEBUG_LOG_H_

