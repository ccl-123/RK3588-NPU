## Compile demo

```sh
$ bash build.sh
```

## Inference demo

This demo integrates retinaface and facenet (MobileFaceNet + ArcFace).

**Model Info:**
- Face Detection: retinaface.rknn
- Face Recognition: w600k_mbf.rknn (512-dim feature, trained on WebFace600K)

```sh
# Generate feature library
$ cd install/face_recognition
$ ./face_recognition data/model/retinaface.rknn data/model/w600k_mbf.rknn 1
```

Feature library will generate in install/face_recognition/data named face_feature_lib.

```sh
# Identification
$ ./face_recognition data/model/retinaface.rknn data/model/w600k_mbf.rknn data/model/lin_1.jpg
```

**Important Notes:**
- When you generate feature library, please make sure only one face in picture.
- The new model (w600k_mbf.rknn) outputs 512-dim features (old facenet.rknn was 128-dim).
- You must regenerate the feature library when switching models.