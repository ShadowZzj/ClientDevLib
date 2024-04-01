/* Copyright 2017 - 2023 R. Thomas
 * Copyright 2017 - 2023 Quarkslab
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LIEF_VERSION_H
#define LIEF_VERSION_H

#if defined(NDEBUG)
  #define LIEF_NAME "LIEF"
#else
  #define LIEF_NAME "LIEF (Debug)"
#endif

#define LIEF_VERSION "0.14.1-bae887e0"
#define LIEF_TAGGED 1
#define LIEF_TAG    "0.14.1"
#define LIEF_COMMIT "bae887e0"

#define HUMAN_VERSION " v" LIEF_VERSION
#define HUMAN_NAME NAME HUMAN_VERSION

#endif
