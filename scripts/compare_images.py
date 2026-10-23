#!/usr/bin/env python3
"""Compare two directories of frame_*.png files from capture_vkquake3.sh.

For each frame, the script prints the largest channel difference and the
percentage of pixels that differ by more than the threshold. It exits with
status 1 if a frame is missing, if the sizes are different, or if a frame has
more different pixels than the limit.

Usage: compare_images.py [-t <threshold>] [-l <percent>] [-d <dir>] <ref dir> <test dir>

  -t <threshold>  A pixel differs if a channel differs by more than this value
                  (0 to 255). The default is 0, an exact comparison.
  -l <percent>    The largest permitted percentage of different pixels in
                  a frame. The default is 0.
  -d <dir>        Write a difference image for each failed frame into <dir>.
"""

import argparse
import pathlib
import sys

from PIL import Image, ImageChops


def compare(ref_path, test_path, threshold):
    """Return (max channel difference, different pixel count, pixel count, diff image)."""
    with Image.open(ref_path) as ref_image, Image.open(test_path) as test_image:
        ref = ref_image.convert("RGB")
        test = test_image.convert("RGB")
    if ref.size != test.size:
        raise ValueError(f"size {ref.size} and {test.size} are different")

    diff = ImageChops.difference(ref, test)
    # The largest channel difference of each pixel.
    channels = diff.split()
    largest = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
    histogram = largest.histogram()
    max_diff = max((value for value, count in enumerate(histogram) if count), default=0)
    different = sum(histogram[threshold + 1:])
    return max_diff, different, ref.size[0] * ref.size[1], diff


def main():
    parser = argparse.ArgumentParser(description="Compare two capture directories.")
    parser.add_argument("-t", "--threshold", type=int, default=0)
    parser.add_argument("-l", "--limit", type=float, default=0.0)
    parser.add_argument("-d", "--diff-dir", type=pathlib.Path)
    parser.add_argument("ref_dir", type=pathlib.Path)
    parser.add_argument("test_dir", type=pathlib.Path)
    args = parser.parse_args()

    ref_frames = sorted(args.ref_dir.glob("frame_*.png"))
    if not ref_frames:
        print(f"No frame_*.png files in {args.ref_dir}", file=sys.stderr)
        return 1

    failed = 0
    for ref_path in ref_frames:
        test_path = args.test_dir / ref_path.name
        if not test_path.exists():
            print(f"{ref_path.name}: MISSING")
            failed += 1
            continue
        try:
            max_diff, different, total, diff = compare(ref_path, test_path, args.threshold)
        except ValueError as error:
            print(f"{ref_path.name}: FAIL, {error}")
            failed += 1
            continue

        percent = 100.0 * different / total
        status = "ok" if percent <= args.limit else "FAIL"
        print(f"{ref_path.name}: {status}, max difference {max_diff}, "
              f"{percent:.3f}% of pixels differ")
        if status == "FAIL":
            failed += 1
            if args.diff_dir:
                args.diff_dir.mkdir(parents=True, exist_ok=True)
                diff.save(args.diff_dir / ref_path.name)

    extra = sorted(set(p.name for p in args.test_dir.glob("frame_*.png"))
                   - set(p.name for p in ref_frames))
    for name in extra:
        print(f"{name}: not in the reference")
        failed += 1

    print(f"{len(ref_frames) - failed} of {len(ref_frames)} frames pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
