## Compile demo

```sh
$ bash build.sh
```

## Inference demo

This demo integrates retinaface and facenet (MobileFaceNet + ArcFace) for real-time face recognition.

**Model Info:**
- Face Detection: retinaface.rknn
- Face Recognition: w600k_mbf.rknn (512-dim feature, trained on WebFace600K)

Before inference, please run face_recognition to generate face_feature_lib and copy the library here.

```sh
# Real-time face recognition with camera
$ lsusb # Check camera device number
$ cd /home/firefly/open_project/edge2-npu/C++/face_recognition_cap/install/face_recognition_cap
$ ./face_recognition_cap data/model/retinaface.rknn data/model/w600k_mbf.rknn usb 21
```

**Parameters:**
- Camera type: `usb` or `mipi`
- Device number: camera device index (e.g., 0, 1, 2...)

**Important Notes:**
- The new model (w600k_mbf.rknn) outputs 512-dim features (old facenet.rknn was 128-dim).
- You must regenerate the feature library when switching models.