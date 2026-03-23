一个基于QT + ffmpeg + opencv + openGL 的视频播放器，使用opencv::dnn + sqlite + onnx 模型进行人脸检测和人脸识别。
1、音视频的同步播放， seek。
2、给视频添加OpenCV自带滤镜。
3、添加人脸检测大模型，对视频中人脸进行抓取并且画框。
4、添加人脸识别大模型。创建人脸sample 数据库，并且对视频中人脸进行识别。
5、支持拉rstp， rtmp流播放。

部分效果展示：
<img width="1920" height="1080" alt="拉流" src="https://github.com/user-attachments/assets/b80afefc-dc0a-4c04-a82f-1cba60c56e30" />
<img width="1920" height="1080" alt="FaceRecognition" src="https://github.com/user-attachments/assets/c73bd9a2-230f-4d28-8c45-ba34c03a5516" />
<img width="1920" height="1080" alt="facedetect" src="https://github.com/user-attachments/assets/0fc2cd20-da82-4300-832c-58349c80e800" />
<img width="1920" height="1080" alt="Filter" src="https://github.com/user-attachments/assets/948e7175-bfb7-4b93-a91c-f117a1f7c1a8" />

更多开发还在进行中...
