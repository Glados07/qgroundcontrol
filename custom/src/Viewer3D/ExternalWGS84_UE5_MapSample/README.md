# Realistic External WGS84 Town Map Sample

This regenerated sample is more realistic than the earlier block-model versions. It keeps the same QGC external-model workflow, but adds UV textures and more street detail.

## Recommended QGC File

Use this file in QGC Viewer3D:

```text
realistic_town_wgs84_map.obj
```

The OBJ references `realistic_town_wgs84_map.mtl` and the `textures/` folder. Keep them together.

## Visual Elements

- Procedural textured ground and grass
- Asphalt roads with worn asphalt texture
- Concrete sidewalks
- Dashed lane markings and crosswalks
- Textured building facades with repeated windows/shopfronts
- Textured roofs
- Small cars with glass and wheels
- Low-poly trees with leaf texture
- Street lamps

## QGC Viewer3D Settings

```text
Origin Latitude:        37.4456000000
Origin Longitude:       -122.1616000000
Origin Altitude:        9.000 m
Model Unit To Meters:   1.0
Model Scale:            1.0
North/Yaw Angle:        0.0 deg
Vehicles Altitude Bias: 0.0 m
```

## Counts

```text
Buildings:    170
Road segments:635
Cars:         55
Trees:        85
Street lamps: 42
Vertices:     41066
Faces:        10300
```

## Source and License

Source status: `downloaded`.

The preferred source is OpenStreetMap data queried with Overpass API. OpenStreetMap data is licensed under ODbL 1.0. Credit OpenStreetMap contributors when using derived data. Procedural textures are generated locally for this sample.

- https://www.openstreetmap.org/copyright
- https://wiki.openstreetmap.org/wiki/Overpass_API

## Limitations

This is a generated external OBJ/FBX sample, not a real photogrammetry city scan and not a packaged UE5 `.umap` scene. It is designed to test QGC Viewer3D external-model loading, WGS84 origin placement, scale, yaw and aircraft/mission overlay.
