/**
 * Copyright 2009 Google Inc.
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

// Author: Bryan McQuade, Matthew Steele

#ifndef JPEG_OPTIMIZER_H_
#define JPEG_OPTIMIZER_H_

#include <string>

namespace pagespeed {

namespace image_compression {

// Performs lossless optimization, that is, the output image will be
// pixel-for-pixel identical to the input image.
bool OptimizeJpeg(const std::string &original,
                  std::string *compressed);

// Performs lossy optimization of a JPEG. Converts JPEGs to
// 4:2:0. Retains color space of input file.
//
// jpeg_quality - Can take values in the range [1,100].
//   For web images the preferred value for quality is 85.
//   For smaller images like thumbnails the preferred value for quality is 75.
//   Setting it to values below 50 is generally not preferable.
bool OptimizeJpegLossy(const std::string &original,
                       std::string *compressed,
                       int jpeg_quality);

}  // namespace image_compression

}  // namespace pagespeed

#endif  // JPEG_OPTIMIZER_H_
