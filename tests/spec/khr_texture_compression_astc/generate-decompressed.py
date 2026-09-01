#!/usr/bin/env python3
#
# Copyright © 2026 Igalia SL
# SPDX-License-Identifier: MIT

"""Regenerate decompressed/ from compressed/ using astcenc.

The khr_compressed_astc-miptree tests compare the GPU's decode of the
compressed/ ASTC miptrees against decompressed/, a matching set of
uncompressed miptrees produced by astcenc, the reference ASTC codec.  This
script regenerates decompressed/ so that the reference data can be rebuilt
when astcenc's decoder is corrected, rather than being an opaque blob.

astcenc reads only the first mipmap of a KTX file, so each level is handed
to it wrapped in a single-level KTX of its own and the results are stitched
back into a miptree here.

Requires astcenc (https://github.com/ARM-software/astc-encoder) in $PATH;
tested with 5.3.0.  Run it from anywhere:

    ./generate-decompressed.py
"""

import argparse
import collections
import os
import struct
import subprocess
import sys
import tempfile

KTX_IDENTIFIER = b'\xabKTX 11\xbb\r\n\x1a\n'
KTX_ENDIAN_LE = 0x04030201

GL_RED = 0x1903
GL_RG = 0x8227
GL_RGB = 0x1907
GL_RGBA = 0x1908
GL_SRGB8_ALPHA8 = 0x8C43
GL_RGB16F = 0x881B
GL_UNSIGNED_BYTE = 0x1401
GL_HALF_FLOAT = 0x140B

# One entry per decompressed/<tree>/<profile> directory.  "flag" is the
# astcenc profile to decode with; the GL fields are the format the miptree
# tests expect to load the reference image as.
Profile = collections.namedtuple(
    'Profile', 'flag gl_type gl_type_size gl_format gl_internal_format texel_size')

PROFILES = {
    # LDR linear: RGB8, stored as GL_RGB/GL_UNSIGNED_BYTE. Note: astcenc
    # post-4.7 (62931077e35050bde5850aa0223a228d9e3c4cde) will default to unorm8
    # decode behavior instead of fp16.  Those images would only be valid if
    # GL_EXT_texture_compression_astc_decode_mode was used to select unorm8
    # decode.  The current images in the tree were generated before that commit,
    # so they correctly use fp16 decode mode.
    #
    # 'ldrl': Profile('-dl', GL_UNSIGNED_BYTE, 1, GL_RGB, GL_RGB, 3),

    # LDR sRGB: RGB8 sRGB-encoded, plus the opaque alpha channel that
    # astcenc elides.  See decode_level().
    'ldrs': Profile('-ds', GL_UNSIGNED_BYTE, 1, GL_RGBA, GL_SRGB8_ALPHA8, 4),

    # HDR: RGB16F, which is what the tests want as-is.
    'hdr': Profile('-dh', GL_HALF_FLOAT, 2, GL_RGB, GL_RGB16F, 6),
}

# astcenc always decodes to RGBA internally, but its writers narrow the
# output to the channels the image actually uses -- see
# determine_image_components() in Source/astcenccli_image.cpp, which drops
# alpha when every texel is opaque and drops G and B when the image is
# greyscale.  That is a property of the decoded content, not a switch: a
# -dsw decompression swizzle is applied before the scan, so no swizzle can
# talk astcenc into writing a channel it has decided is redundant.  The
# channel count in the output header is therefore how we learn what the
# alpha channel held.
COMPONENTS_OF_GL_FORMAT = {
    GL_RED: 1,
    GL_RG: 2,
    GL_RGB: 3,
    GL_RGBA: 4,
}

TREES = ('2D', 'SLICED3D')

BLOCK_DIMS = ('4x4', '5x4', '5x5', '6x5', '6x6', '8x5', '8x6', '8x8',
              '10x5', '10x6', '10x8', '10x10', '12x10', '12x12')


def align4(n):
    return (n + 3) & ~3


class Ktx(object):
    """Just enough KTX for these files: no key/value data, no arrays or
    cube faces, and mip levels stored innermost-first."""

    def __init__(self, path=None):
        if path is None:
            return
        d = open(path, 'rb').read()
        if d[:12] != KTX_IDENTIFIER:
            raise ValueError('%s: not a KTX file' % path)
        if struct.unpack('<I', d[12:16])[0] != KTX_ENDIAN_LE:
            raise ValueError('%s: not little-endian' % path)
        (self.gl_type, self.gl_type_size, self.gl_format,
         self.gl_internal_format, self.gl_base_internal_format,
         self.width, self.height, self.depth, self.array_elements,
         self.faces, self.num_levels, kv_bytes) = struct.unpack('<12I', d[16:64])
        if kv_bytes or self.array_elements or self.faces > 1:
            raise ValueError('%s: unsupported KTX features' % path)

        self.levels = []
        off = 64
        for _ in range(self.num_levels):
            size = struct.unpack('<I', d[off:off + 4])[0]
            off += 4
            self.levels.append(d[off:off + size])
            off += align4(size)

    def level_size(self, level):
        """Dimensions of a mip level.  Depth of 0 means a 2D texture, which
        stays 2D; a sliced-3D texture halves its depth like any other axis."""
        w = max(self.width >> level, 1)
        h = max(self.height >> level, 1)
        d = max(self.depth >> level, 1) if self.depth else 0
        return w, h, d

    def write(self, path):
        out = bytearray(KTX_IDENTIFIER)
        out += struct.pack('<13I', KTX_ENDIAN_LE,
                           self.gl_type, self.gl_type_size, self.gl_format,
                           self.gl_internal_format, self.gl_base_internal_format,
                           self.width, self.height, self.depth,
                           self.array_elements, self.faces, self.num_levels, 0)
        for blob in self.levels:
            out += struct.pack('<I', len(blob)) + blob
            out += b'\0' * (align4(len(blob)) - len(blob))
        open(path, 'wb').write(bytes(out))


def single_level_ktx(src, level, path):
    """Write one mip level of a compressed miptree as a KTX of its own,
    because astcenc only reads the first mipmap of an input file."""
    out = Ktx()
    out.gl_type = src.gl_type                     # 0 for compressed formats
    out.gl_type_size = src.gl_type_size
    out.gl_format = src.gl_format                 # 0 for compressed formats
    out.gl_internal_format = src.gl_internal_format
    out.gl_base_internal_format = src.gl_base_internal_format
    out.width, out.height, out.depth = src.level_size(level)
    out.array_elements = 0
    out.faces = 1
    out.num_levels = 1
    out.levels = [src.levels[level]]
    out.write(path)


def astcenc(flag, src, dst):
    subprocess.run(['astcenc', flag, src, dst], check=True, capture_output=True)


def decode_level(compressed, level, profile, tmp, label):
    """Decode one mip level, returning it padded to the KTX row alignment."""
    src = os.path.join(tmp, 'level.ktx')
    dst = os.path.join(tmp, 'plain.ktx')
    single_level_ktx(compressed, level, src)
    astcenc(profile.flag, src, dst)

    w, h, d = compressed.level_size(level)
    plain = Ktx(dst)
    texels = plain.levels[0]

    got = COMPONENTS_OF_GL_FORMAT.get(plain.gl_format)
    want = profile.texel_size // profile.gl_type_size
    if got is None:
        raise SystemExit('%s level %d: astcenc wrote unhandled format 0x%x'
                         % (label, level, plain.gl_format))
    if got == want - 1 and want == 4:
        # astcenc narrowed RGBA to RGB, which it only does when every texel
        # decoded to an opaque alpha, so restoring 0xff is exact rather than
        # an assumption.
        texels = b''.join(texels[i * 3:i * 3 + 3] + b'\xff'
                          for i in range(len(texels) // 3))
    elif got != want:
        # Fewer channels than that means a greyscale level, which would need
        # channel expansion this script does not implement.  Nothing in this
        # image set hits it, so fail loudly rather than silently misparse.
        raise SystemExit('%s level %d: astcenc wrote %d channels, expected %d'
                         % (label, level, got, want))

    # KTX rows are padded to a 4 byte boundary (GL_UNPACK_ALIGNMENT).
    src_pitch = w * profile.texel_size
    dst_pitch = align4(src_pitch)
    if src_pitch == dst_pitch:
        return texels
    pad = b'\0' * (dst_pitch - src_pitch)
    rows = len(texels) // src_pitch
    return b''.join(texels[r * src_pitch:(r + 1) * src_pitch] + pad
                    for r in range(rows))


def regenerate(base, tree, name, block_dim, tmp):
    profile = PROFILES[name]
    rel = os.path.join(tree, name, 'waffles-%s.ktx' % block_dim)
    compressed = Ktx(os.path.join(base, 'compressed', rel))

    out = Ktx()
    out.gl_type = profile.gl_type
    out.gl_type_size = profile.gl_type_size
    out.gl_format = profile.gl_format
    out.gl_internal_format = profile.gl_internal_format
    out.gl_base_internal_format = GL_RGBA if profile.texel_size == 4 else GL_RGB
    out.width, out.height = compressed.width, compressed.height
    out.depth = compressed.depth
    out.array_elements = 0
    out.faces = 1
    out.num_levels = compressed.num_levels
    out.levels = [decode_level(compressed, l, profile, tmp, rel)
                  for l in range(compressed.num_levels)]

    path = os.path.join(base, 'decompressed', rel)
    old = open(path, 'rb').read() if os.path.exists(path) else None
    out.write(path)
    return rel, old, open(path, 'rb').read()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--dir', default=os.path.dirname(os.path.abspath(__file__)),
                        help='directory holding compressed/ and decompressed/')
    parser.add_argument('--profile', choices=sorted(PROFILES), action='append',
                        help='only regenerate this profile (repeatable)')
    args = parser.parse_args()

    try:
        version = subprocess.run(['astcenc', '-version'],
                                 check=True, capture_output=True,
                                 text=True).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError):
        sys.exit('astcenc not found in $PATH; see '
                 'https://github.com/ARM-software/astc-encoder')
    print('using %s' % version)

    names = args.profile or sorted(PROFILES)
    deltas = collections.Counter()
    changed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for tree in TREES:
            for name in names:
                for block_dim in BLOCK_DIMS:
                    rel, old, new = regenerate(args.dir, tree, name,
                                               block_dim, tmp)
                    if old == new:
                        continue
                    changed += 1
                    print('  regenerated %s' % rel)
                    if old is not None and len(old) == len(new):
                        deltas.update(b - a for a, b in zip(old, new))

    print()
    if not changed:
        print('all reference images already up to date')
    else:
        print('%d files changed; byte deltas: %s'
              % (changed, dict(sorted(deltas.items()))))


if __name__ == '__main__':
    main()
