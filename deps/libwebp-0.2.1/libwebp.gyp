# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'use_system_libwebp%': 0,
  },
  'conditions': [
    ['use_system_libwebp==0', {
      'targets': [
        {
          'target_name': 'libwebp_dec',
          'type': 'static_library',
          'include_dirs': ['.'],
          'sources': [
            'dec/alpha.c',
            'dec/buffer.c',
            'dec/frame.c',
            'dec/idec.c',
            'dec/io.c',
            'dec/layer.c',
            'dec/quant.c',
            'dec/tree.c',
            'dec/vp8.c',
            'dec/vp8l.c',
            'dec/webp.c',
          ],
        },
        {
          'target_name': 'libwebp_dsp',
          'type': 'static_library',
          'include_dirs': ['.'],
          'sources': [
            'dsp/cpu.c',
            'dsp/dec.c',
            'dsp/dec_sse2.c',
            'dsp/enc.c',
            'dsp/enc_sse2.c',
            'dsp/lossless.c',
            'dsp/upsampling.c',
            'dsp/upsampling_sse2.c',
            'dsp/yuv.c',
          ],
        },
        {
          'target_name': 'libwebp_enc',
          'type': 'static_library',
          'include_dirs': ['.'],
          'sources': [
            'enc/alpha.c',
            'enc/analysis.c',
            'enc/backward_references.c',
            'enc/config.c',
            'enc/cost.c',
            'enc/filter.c',
            'enc/frame.c',
            'enc/histogram.c',
            'enc/iterator.c',
            'enc/layer.c',
            'enc/picture.c',
            'enc/quant.c',
            'enc/syntax.c',
            'enc/tree.c',
            'enc/vp8l.c',
            'enc/webpenc.c',
          ],
        },
        {
          'target_name': 'libwebp_utils',
          'type': 'static_library',
          'include_dirs': ['.'],
          'sources': [
            'utils/bit_reader.c',
            'utils/bit_writer.c',
            'utils/color_cache.c',
            'utils/filters.c',
            'utils/huffman.c',
            'utils/huffman_encode.c',
            'utils/quant_levels.c',
            'utils/rescaler.c',
            'utils/thread.c',
            'utils/utils.c',
          ],
        },
        {
          'target_name': 'libwebp',
          'type': 'none',
          'dependencies' : [
            'libwebp_dec',
            'libwebp_dsp',
            'libwebp_enc',
            'libwebp_utils',
          ],
          'direct_dependent_settings': {
            'include_dirs': ['.'],
          },
          'conditions': [
            ['OS!="win"', {'product_name': 'webp'}],
          ],
        },
      ],
    }, {
      'targets': [
        {
          'target_name': 'libwebp',
          'type': 'none',
          'direct_dependent_settings': {
            'defines': [
              'ENABLE_WEBP',
            ],
          },
          'link_settings': {
            'libraries': [
              '-lwebp',
            ],
          },
        }
      ],
    }],
  ],
}
