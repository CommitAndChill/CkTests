"""Reproduce a finer actual-glass-cloth revision without changing any V1 bytes."""
from __future__ import annotations
import argparse
import copy
import io
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
SOURCE_ROOT = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / 'Tools/UsfMaterials'))
import source_batch2_textures as source


def png(image):
    buffer = io.BytesIO()
    image.save(buffer, format='PNG', compress_level=9)
    return buffer.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
    output = parser.parse_args().output.resolve()
    original_manifest = (SOURCE_ROOT / 'Manifest.json').read_bytes()
    manifest = json.loads(original_manifest)
    original_record = manifest['materials']['fiberglass']
    photo_record = original_record['originals']['Photograph']
    photograph = (SOURCE_ROOT / photo_record['path']).read_bytes()
    if source.sha256(photograph) != photo_record['sha256']:
        raise RuntimeError('Untouched original photograph hash differs')
    original = source.Image.open(io.BytesIO(photograph)).convert('RGB')
    if original.size != (1710, 923):
        raise RuntimeError('Unexpected original photo dimensions')
    # V1 used the right550g/m2 cloth. This crop selects the genuinely finer
    # left220g/m2 cloth, above the ruler and away from the green boundary/coin.
    crop_box = (96, 16, 544, 464)
    crop_image = original.crop(crop_box)
    crop = source.np.asarray(crop_image)
    tile = source.np.concatenate((crop, crop[:, ::-1]), axis=1)
    tile = source.np.concatenate((tile, tile[::-1]), axis=0)
    tile = source.Image.fromarray(tile).resize((1024, 1024), source.Image.Resampling.LANCZOS)
    srgb = source.np.asarray(tile, dtype=source.np.float64) / 255.0
    linear = source.np.where(srgb <= .04045, srgb / 12.92, ((srgb + .055) / 1.055) ** 2.4)
    luminance = linear @ source.np.array([.2126, .7152, .0722])
    frequency = source.np.fft.fftfreq(1024)
    gaussian = source.np.exp(-2 * source.np.pi ** 2 * 32 ** 2 * (frequency[:, None] ** 2 + frequency[None, :] ** 2))
    low = source.np.fft.ifft2(source.np.fft.fft2(luminance) * gaussian).real
    ratio = (luminance + .02) / (low + .02)
    detail = source.np.log(ratio)
    p1, p99 = source.np.percentile(detail, [1, 99])
    height = source.np.clip(.5 + .5 * detail / (p99 - p1), 0, 1)
    # Preserve actual fine bright bundles/dark gaps rather than compressing
    # source photo structure into V1's low-contrast height-colored brick bands.
    base_linear = source.np.clip(.55 * ratio ** .8, .04, .92)
    base = source.np.where(base_linear <= .0031308, base_linear * 12.92, 1.055 * base_linear ** (1 / 2.4) - .055)
    du = (source.np.roll(height, -1, 1) - source.np.roll(height, 1, 1)) * 512
    dv = (source.np.roll(height, -1, 0) - source.np.roll(height, 1, 0)) * 512
    normal = source.np.stack((-.0006 * du, -.0006 * dv, source.np.ones_like(height)), axis=-1)
    normal /= source.np.linalg.norm(normal, axis=-1, keepdims=True)
    roughness = source.np.clip(.32 + .12 * (1 - height), 0, 1)
    images = {'BaseColor': source.Image.fromarray(source.np.repeat(source.np.rint(base * 255).astype('uint8')[:, :, None], 3, axis=2)),
              'Normal': source.Image.fromarray(source.np.rint((normal * .5 + .5) * 255).astype('uint8')),
              'Roughness': source.Image.fromarray(source.np.rint(roughness * 255).astype('uint8')),
              'Height': source.Image.fromarray(source.np.rint(height * 65535).astype('uint16'))}
    recipe = {
        'version': 2, 'source_manifest_sha256': source.sha256(original_manifest), 'source_photo_sha256': photo_record['sha256'],
        'source_photo_path': photo_record['path'], 'source_url': original_record['source_url'],
        'license': original_record['license'], 'rights_unchanged': 'Original author PD-self release and exact rights/metadata/original remain in V1 receipts; not relabeledCC0.',
        'source_identity': 'Actual finer220g/m2 plain0/90 glass reinforcement cloth at photo left; raw glass weave beneath independently authored resin. Not a cured-resin PBR scan.',
        'crop_xyxy': list(crop_box), 'crop_dimensions': [448, 448],
        'tiling': 'Concatenate crop+Xmirror, then row+Ymirror ->896square, Lanczos resize1024square. No new weave grid/noise. Mirrored repetition explicit; not1024native unique photo resolution.',
        'illumination': 'Decode sRGB to linear; Y=.2126R+.7152G+.0722B; low=periodicFFTGaussian(Y,sigma32px); ratio=(Y+.02)/(low+.02); detail=log(ratio).',
        'base_color': 'linearRGB=clamp(.55*ratio^.8,.04,.92).xxx then sRGB8. Preserves fine photographed bundle/gap contrast; removes chroma and broad lighting approximately, not measured albedo.',
        'height': 'H=clamp(.5+.5*detail/(percentile99(detail)-percentile1(detail)),0,1); encodeuint16. Uncalibrated brightness relief surrogate, not measured height.',
        'normal': 'Periodic central derivatives=(next-prev)*512; N=normalize(-.0006*dHdu,-.0006*dHdv,1); encodeRGB8. Constructed projectDirectX convention; derived uncalibrated normal, not scan.',
        'roughness': 'clamp(.32+.12*(1-H),0,1), encodeL8. Authored variation surrogate, not measured roughness.',
        'detail_percentiles_1_99': [float(p1), float(p99)], 'height_mean_std': [float(height.mean()), float(height.std())],
        'base_linear_mean_std': [float(base_linear.mean()), float(base_linear.std())],
        'normal_XY_amplitude_p99': float(source.np.percentile(source.np.linalg.norm(normal[:, :, :2], axis=2), 99)),
        'generator_sha256': source.sha256(Path(__file__).read_bytes()),
        'verification_scope': 'Actual finer photo region and authored-map structure inspected; crop excludes ruler/coin/green edge. All data uncalibrated; runtime visual acceptance remains separate.'}
    source.immutable(output / 'Recipe.json', (json.dumps(recipe, indent=2, sort_keys=True) + '\n').encode())
    record = copy.deepcopy(original_record)
    record['maps'] = {}
    record['identity_notes'] = 'V2 uses actual finer220g/m2 raw glass cloth at source-photo left, preserving fine bundle crossovers/dark gaps. Uncalibrated photo-derived BC/N/R/relativeHeight under independently authored resin; no cured-resin scan/physical calibration claim.'
    record['derivation_recipe'] = recipe
    for channel, image in images.items():
        entry = source.save_map(output.parent, output.name, 'LibraryFiberglass', channel, png(image), {
            'source_sha256': photo_record['sha256'], 'modifications': 'Photo-derivedV2; see fiberglass_v2/Recipe.json for exact finer crop/tiling/map formulas. Original bytes and V1 maps preserved.'})
        entry['runtime_name'] = 'T_LibraryFiberglassV2_' + channel
        record['maps'][channel] = entry
    for detail_sheet in (False, True):
        canvas = source.Image.new('RGB', (1280, 286), (30, 30, 34))
        draw = source.ImageDraw.Draw(canvas)
        for i, (channel, image) in enumerate([('ActualFinePhotoCrop', crop_image)] + list(images.items())):
            if channel == 'Height':
                image = source.Image.fromarray(source.np.rint(source.np.asarray(image, dtype=float) / 257).astype('uint8'))
            if detail_sheet:
                if channel == 'ActualFinePhotoCrop':
                    image = image.crop((96, 96, 352, 352))
                else:
                    image = image.crop((128, 128, 384, 384))
            canvas.paste(image.convert('RGB').resize((256, 256), source.Image.Resampling.NEAREST if detail_sheet else source.Image.Resampling.LANCZOS), (i * 256, 25))
            draw.text((i * 256 + 4, 5), channel, fill='white')
        source.immutable(output / ('DetailSheet.png' if detail_sheet else 'Sheet.png'), png(canvas))
    revision = copy.deepcopy(manifest)
    revision['materials']['fiberglass'] = record
    for key in manifest['materials']:
        if key != 'fiberglass' and revision['materials'][key] != manifest['materials'][key]:
            raise RuntimeError('Unowned material record changed: ' + key)
    revision['revision'] = {'name': 'fiberglass_v2', 'source_manifest_sha256': source.sha256(original_manifest),
                            'recipe_path': 'fiberglass_v2/Recipe.json', 'recipe_sha256': source.sha256((output / 'Recipe.json').read_bytes()),
                            'generator_path': 'fiberglass_v2/derive.py', 'generator_sha256': recipe['generator_sha256'],
                            'scope': 'Onlyfiberglass record changed. ExistinglookID LibraryFiberglass; runtimeT_LibraryFiberglassV2_*; oldtextures/maps/Manifest/source remain untouched.'}
    path = output.parent / 'Manifest-v2.json'
    source.immutable(path, (json.dumps(revision, indent=2, sort_keys=True) + '\n').encode())
    print('Fiberglass Manifest-v2 SHA256', source.sha256(path.read_bytes()))


if __name__ == '__main__':
    main()
