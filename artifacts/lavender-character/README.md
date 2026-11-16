# 紫发角色 — 独立静态初模

根据本次提供的参考图从零以程序化网格制作，没有使用项目 Remiel 模型。当前为简化手办风格的造型初稿，和参考图的精细二次元脸部、布料、头发、人体细节有明显差距，不是高保真复刻或游戏成品。

- `lavender-character.blend`：可编辑源文件，CHARACTER 集合只含角色，另有摄影棚灯光／相机／底座。
- `lavender-character.glb`：角色网格和嵌入式纯色 PBR 材质，不含底座。
- `lavender-character.obj` + `.mtl`：通用静态模型。
- `preview.png` / `front.png` / `back.png`：Blender Cycles 渲染。
- `create_model.py`：生成脚本，使用 Blender 4.5.3。重新运行会覆盖本目录生成物。

模型按约 1.7 米尺度制作，可以在 Blender 中缩放。静态分件网格，未绑定骨骼，没有动画、面部表情、纹理贴图或为变形优化的拓扑；不承诺图中标注的 7.9 万三角形／单材质规格。未接入 lc1。

## 旋转预览

在此目录执行 `python3 -m http.server 18846 --bind 127.0.0.1`，打开 http://127.0.0.1:18846/preview.html 。拖动旋转，滚轮缩放。页面使用本地附带的 Google model-viewer 4.0.0（Apache-2.0，https://github.com/google/model-viewer），不需要上传模型。

已完成 Blender 导出与正背面渲染检查，浏览器确认 GLB 显示。需要继续精修脸型／眼睛、头发层次、裙褶和手脚解剖。
