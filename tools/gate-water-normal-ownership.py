#!/usr/bin/env python3
"""Verify dry terrain retains its normals while true water retains plane ownership.

This is a CPU-only gate over actual renderdump buffers, the GPU C3DFLD16 coast
field and (for a fixed build) its C3DOPAQ1 screen opaque-terrain mask. It does not
launch a renderer. Coordinates and ROIs use top-left screenshot pixels; binary
GL rows are flipped on input. No colour/hue heuristic participates in the gate.

The known SCB01EA regression uses cam27,30, dist1920, 2400x1600, and dry ROI
300 1100 1300 1500. Its old normal helper overwrites 186,586 dry cliff pixels as
water despite source distance < -3 cells. A same-camera --reference captured
with the water-normal helper disabled additionally proves the original material,
normal and depth survive. --opaque-mask checks actual projected terrain, rather
than looking up a source cut at the lower water plane.

Example (capture/reference are filename prefixes, not directories):
  python3 tools/gate-water-normal-ownership.py --capture /qa/on/capture \
    --reference /qa/off/capture --field /qa/on/field16.bin \
    --opaque-mask /qa/on/opaque.bin --log /qa/on/run.log \
    --dry-roi 300 1100 1300 1500 --output /qa/result.json

The wet control excludes object-class pixels with a 2px support halo. Its depth
check uses the logged level(s) only for fixtures with at most two bodies (where
the logged endpoints list every level), or explicit --plane-levels. This guards
normal ownership and visible water-plane preservation; full per-body hydrology replay is a
separate gate. Absence of a reference/mask is recorded, never implied as proof.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import re
import struct
import sys

import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def words(data, offset, count, version=1):
    for endian in ('<', '>'):
        values = struct.unpack_from(endian + str(count) + 'I', data, offset)
        if values[0] == version:
            return endian, values
    raise ValueError('Unsupported binary version or byte order')


def read_buffer(prefix, kind):
    path = Path(str(prefix) + '-' + kind + '.bin.gz')
    data = gzip.open(path, 'rb').read()
    if len(data) < 12:
        raise ValueError(f'Truncated renderdump: {path}')
    width, height, fmt = struct.unpack_from('<3I', data)
    if fmt != (1 if kind == 'depth' else 2):
        raise ValueError(f'Unexpected renderdump channel format: {path}')
    channels = 1 if fmt == 1 else 4
    if not (0 < width <= 16384 and 0 < height <= 16384):
        raise ValueError(f'Invalid renderdump size: {path}')
    if len(data) != 12 + width * height * channels * 4:
        raise ValueError(f'Wrong renderdump payload length: {path}')
    array = np.frombuffer(data, '<f4', offset=12).reshape(height, width, channels)[::-1]
    if not np.isfinite(array).all():
        raise ValueError(f'Nonfinite renderdump values: {path}')
    return array, path


def read_field(path):
    data = path.read_bytes()
    if len(data) < 28 or data[:8] != b'C3DFLD16':
        raise ValueError('Expected C3DFLD16 field dump')
    endian, (version, width, height, resolution, distance_range) = words(data, 8, 5)
    if not (2 <= width <= 8192 and 2 <= height <= 8192 and 0 < resolution <= 64):
        raise ValueError('Invalid coast field dimensions/resolution')
    if distance_range != 16 or len(data) != 28 + width * height * 8:
        raise ValueError('Expected complete v1 range16/RGBA16 coast field')
    array = np.frombuffer(data, endian + 'u2', offset=28).reshape(height, width, 4)
    return array.astype(np.float64) / 65535, resolution, distance_range


def read_opaque(path, shape):
    data = path.read_bytes()
    if len(data) < 20 or data[:8] != b'C3DOPAQ1':
        raise ValueError('Expected C3DOPAQ1 screen opaque mask')
    _, (version, width, height) = words(data, 8, 3)
    if (height, width) != shape or len(data) != 20 + width * height * 4:
        raise ValueError('Opaque mask size differs from renderdump')
    array = np.frombuffer(data, 'u1', offset=20).reshape(height, width, 4)[::-1]
    if np.any(array[..., :3] != 0) or np.any((array[..., 3] != 0) & (array[..., 3] != 255)):
        raise ValueError('Opaque RT must have zero RGB and binary 0/255 alpha')
    return array[..., 3] == 255


def world_positions(depth, matrix):
    height, width = depth.shape
    yy, xx = np.indices(depth.shape)
    clip = np.stack(((xx + .5) / width * 2 - 1,
                     1 - (yy + .5) / height * 2,
                     depth * 2 - 1, np.ones_like(depth)), axis=-1)
    transformed = clip @ matrix.T
    valid = np.abs(transformed[..., 3]) > 1e-12
    world = transformed[..., :3] / np.where(valid, transformed[..., 3], 1)[..., None]
    return world, valid & (depth >= 0) & (depth < .9999)


def field_at(field, world, resolution, distance_range):
    height, width = field.shape[:2]
    # Clamp the floating coordinate before splitting it, matching GL edge clamp.
    fx = np.clip(world[..., 0] * resolution - .5, 0, width - 1)
    fz = np.clip(world[..., 2] * resolution - .5, 0, height - 1)
    x = np.minimum(np.floor(fx).astype(int), width - 2)
    z = np.minimum(np.floor(fz).astype(int), height - 2)
    a, b = (fx - x)[..., None], (fz - z)[..., None]
    values = ((field[z, x] * (1 - a) + field[z, x + 1] * a) * (1 - b)
              + (field[z + 1, x] * (1 - a) + field[z + 1, x + 1] * a) * b)
    inside = ((world[..., 0] >= 0) & (world[..., 2] >= 0)
              & (world[..., 0] < width / resolution)
              & (world[..., 2] < height / resolution))
    return (values[..., 0] - .5) * 2 * distance_range, values[..., 3], inside


def water_class(normals):
    return (normals[..., 3] > .26) & (normals[..., 3] < .30)


def object_halo(normals, radius=2):
    objects = (normals[..., 3] > .01) & (normals[..., 3] < .75) & ~water_class(normals)
    height, width = objects.shape
    padded = np.pad(objects, radius)
    result = np.zeros_like(objects)
    for y in range(radius * 2 + 1):
        for x in range(radius * 2 + 1):
            result |= padded[y:y + height, x:x + width]
    return result


def vector_normals(normals):
    vectors = normals[..., :3].astype(np.float64) * 2 - 1
    return vectors / np.maximum(np.linalg.norm(vectors, axis=-1, keepdims=True), 1e-12)


def quantiles(values):
    return np.quantile(values, [0, .01, .5, .99, 1]).tolist() if values.size else []


def verify(args):
    errors = []
    paths = []
    normal, path = read_buffer(args.capture, 'normals'); paths.append(path)
    depth, path = read_buffer(args.capture, 'depth'); paths.append(path)
    depth = depth[..., 0]
    if normal.shape[:2] != depth.shape:
        raise ValueError('Normal and depth renderdump dimensions differ')
    matrix_path = Path(str(args.capture) + '-matrices.json'); paths.append(matrix_path)
    matrices = json.loads(matrix_path.read_text())
    matrix = np.asarray(matrices['inverse_vp'], dtype=np.float64).reshape(4, 4, order='F')
    if not np.isfinite(matrix).all():
        raise ValueError('Nonfinite inverse camera matrix')
    field, resolution, distance_range = read_field(args.field); paths.append(args.field)
    world, valid = world_positions(depth, matrix)
    sd, wet, inside = field_at(field, world, resolution, distance_range)
    valid &= inside
    height, width = depth.shape
    x0, y0, x1, y1 = args.dry_roi
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError('Dry ROI lies outside renderdump')
    roi = np.zeros(depth.shape, dtype=bool); roi[y0:y1, x0:x1] = True
    dry_source = roi & valid & (wet <= .001) & (sd < -.5)
    water = water_class(normal)
    reference_report = None
    reference_normal = None
    if args.reference:
        reference_normal, path = read_buffer(args.reference, 'normals'); paths.append(path)
        reference_depth, path = read_buffer(args.reference, 'depth'); paths.append(path)
        reference_depth = reference_depth[..., 0]
        reference_matrix_path = Path(str(args.reference) + '-matrices.json'); paths.append(reference_matrix_path)
        reference_matrices = json.loads(reference_matrix_path.read_text())
        if reference_normal.shape != normal.shape or reference_depth.shape != depth.shape:
            raise ValueError('Reference renderdump size differs')
        if not np.allclose(reference_matrices['inverse_vp'], matrices['inverse_vp'], rtol=0, atol=1e-9):
            raise ValueError('Reference camera is not identical')
        # Select expected land using the independent opaque-only reference, never
        # by requiring the candidate class to be correct already.
        reference_world, reference_valid = world_positions(reference_depth, matrix)
        reference_sd, reference_wet, reference_inside = field_at(field, reference_world, resolution, distance_range)
        dry = (roi & reference_valid & reference_inside & (reference_wet <= .001)
               & (reference_sd < -.5) & (reference_normal[..., 3] >= .85))
        sd_for_dry = reference_sd
        class_difference = np.abs(normal[..., 3] - reference_normal[..., 3])
        dot = np.sum(vector_normals(normal) * vector_normals(reference_normal), axis=-1)
        depth_difference = np.abs(depth - reference_depth)
        reference_report = dict(eligible=int(dry.sum()),
            changed_class_pixels=int((dry & (class_difference > args.class_tolerance)).sum()),
            changed_normal_pixels=int((dry & (dot < args.normal_dot_min)).sum()),
            changed_depth_pixels=int((dry & (depth_difference > args.depth_tolerance)).sum()),
            normal_dot_quantiles=quantiles(dot[dry]),
            depth_difference_quantiles=quantiles(depth_difference[dry]))
        for key in ('changed_class_pixels', 'changed_normal_pixels', 'changed_depth_pixels'):
            if reference_report[key]:
                errors.append(f'Opaque reference {key}: {reference_report[key]}')
    else:
        # Diagnostic fallback for old builds that predate the owner RT. Both
        # expected terrain and erroneous water classes remain eligible.
        dry = dry_source & ((normal[..., 3] >= .85) | water)
        sd_for_dry = sd
    dry_count = int(dry.sum())
    false_water = dry & water
    if dry_count < args.min_dry:
        errors.append(f'Insufficient independent dry support: {dry_count} < {args.min_dry}')
    if false_water.any():
        errors.append(f'Water normals overwrite {int(false_water.sum())} independently dry terrain pixels')
    mask_report = None
    opaque = None
    if args.opaque_mask:
        opaque = read_opaque(args.opaque_mask, depth.shape); paths.append(args.opaque_mask)
        missing = dry & ~opaque
        overwritten = opaque & water
        mask_report = dict(opaque_pixels=int(opaque.sum()), dry_reference_missing=int(missing.sum()),
                           water_class_on_opaque=int(overwritten.sum()))
        if missing.any():
            errors.append(f'Opaque owner mask misses {int(missing.sum())} expected dry pixels')
        if overwritten.any():
            errors.append(f'Water class survives on {int(overwritten.sum())} actual opaque terrain pixels')
    elif args.require_mask:
        errors.append('Required opaque owner RT was not supplied')
    if args.require_reference and not args.reference:
        errors.append('Required opaque-only reference was not supplied')

    wet_source = valid & (wet >= .999) & (sd > .30)
    # A candidate must not hide a newly misclassified wet pixel by labelling it
    # as an object. Prefer the independent reference for object exclusions.
    excluded = object_halo(reference_normal if reference_normal is not None else normal)
    if opaque is not None:
        if reference_normal is not None:
            conflicts = wet_source & water_class(reference_normal) & opaque
            if conflicts.any():
                errors.append(f'Opaque owner mask covers {int(conflicts.sum())} reference-visible true-wet pixels')
        excluded |= opaque
    eligible = wet_source & ~excluded
    count = int(eligible.sum())
    bad_class = eligible & ~water
    if count < args.min_wet:
        errors.append(f'Insufficient unobscured true-wet support: {count} < {args.min_wet}')
    if bad_class.any():
        errors.append(f'Water normal class is missing on {int(bad_class.sum())} true-wet pixels')
    levels = args.plane_levels
    plane_source = 'explicit CLI' if levels else None
    if not levels and args.log:
        paths.append(args.log)
        matches = re.findall(r'WATER\|plane\|[^\n]*?bodies=(\d+)\|[^\n]*?y=([-+.\d]+)\.\.([-+.\d]+)', args.log.read_text())
        if not matches:
            errors.append('No runtime WATER|plane level record in log')
        else:
            bodies, low, high = matches[-1]
            if int(bodies) in (1, 2):
                levels = sorted(set((float(low), float(high))))
                plane_source = 'runtime WATER|plane endpoints; at most two bodies'
            else:
                errors.append('Log has more than two bodies; supply exact --plane-levels for this fixture')
    wet_report = dict(source_pixels=int(wet_source.sum()), exclusions=int((wet_source & excluded).sum()),
                      eligible=count, water_class_pixels=int((eligible & water).sum()),
                      missing_water_class=int(bad_class.sum()), levels=levels, level_source=plane_source)
    if levels:
        levels = np.asarray(levels, dtype=np.float64)
        if not np.isfinite(levels).all():
            raise ValueError('Nonfinite plane levels')
        # The opaque bed is intentionally .04 below the live plane. Choosing the
        # closest complete logged level is valid for this <=2-body fixture only.
        bed_error = np.min(np.abs(world[..., 1, None] - (levels - args.bed_gap)), axis=-1)
        violations = eligible & (bed_error > args.bed_tolerance)
        wet_report.update(nonflat_bed_pixels=int(violations.sum()),
                          bed_error_quantiles=quantiles(bed_error[eligible]))
        if violations.any():
            errors.append(f'{int(violations.sum())} true-wet pixels do not retain a flat water bed')
    else:
        errors.append('Wet control requires --log or --plane-levels')
    return dict(status='FAIL' if errors else 'PASS', errors=errors, size=[width, height],
        dry=dict(roi=args.dry_roi, roi_pixels=int(roi.sum()), source_dry_pixels=int(dry_source.sum()),
                 eligible=dry_count, false_water_pixels=int(false_water.sum()),
                 false_water_sd_quantiles=quantiles(sd_for_dry[false_water])),
        reference=reference_report, owner_mask=mask_report, wet=wet_report,
        thresholds=dict(min_dry=args.min_dry, min_wet=args.min_wet,
            water_class_open_interval=[.26, .30], dry_sd_less_than=-.5, wet_sd_greater_than=.30,
            field_dry_alpha_max=.001, field_wet_alpha_min=.999,
            class_tolerance=args.class_tolerance, normal_dot_min=args.normal_dot_min,
            depth_tolerance=args.depth_tolerance, bed_gap=args.bed_gap, bed_tolerance=args.bed_tolerance),
        proof=dict(actual_gpu_field=True, projected_opaque_mask=opaque is not None,
                   paired_opaque_reference=args.reference is not None),
        inputs={str(path.resolve()): digest(path) for path in paths},
        checker_sha256=digest(Path(__file__)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--capture', required=True, type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--field', required=True, type=Path)
    parser.add_argument('--opaque-mask', type=Path)
    parser.add_argument('--require-mask', action='store_true')
    parser.add_argument('--require-reference', action='store_true')
    parser.add_argument('--log', type=Path)
    parser.add_argument('--plane-levels', nargs='+', type=float)
    parser.add_argument('--dry-roi', required=True, nargs=4, type=int, metavar=('X0', 'Y0', 'X1', 'Y1'))
    parser.add_argument('--min-dry', type=int, default=100)
    parser.add_argument('--min-wet', type=int, default=50)
    parser.add_argument('--class-tolerance', type=float, default=1.01 / 255)
    parser.add_argument('--normal-dot-min', type=float, default=.999)
    parser.add_argument('--depth-tolerance', type=float, default=2e-7)
    parser.add_argument('--bed-gap', type=float, default=0.0, help='Legacy captures used a .04-cell floor as depth; current water writes its actual surface depth')
    parser.add_argument('--bed-tolerance', type=float, default=.02)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        result = verify(args)
    except Exception as exc:
        result = dict(status='ERROR', errors=[str(exc)], checker_sha256=digest(Path(__file__)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
