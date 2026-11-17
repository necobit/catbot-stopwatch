# CatbotWalk — M5StopWatch のちいさな世界

M5Stack StopWatch (C152) の丸い画面 (466×466 AMOLED) に、小さな丸い島の
俯瞰ワールドを表示し、Catbot がその上を勝手におさんぽするプログラムです。
ボールケース＋アクリルドーム越しに眺める想定です。

- モデル: `Catbot_____1body.step`（本人の CAD）を GLB 化し、俯瞰視点の
  8方向スプライトに描き起こしたものを使用（`render_sprites.py` で再生成可能）
- 操作: 画面タップ or KEY A = ジャンプ / KEY B = 昼・夜の切り替え
- Catbot は島の上を自動で散歩します（池と島の外周では自動で引き返します）

## 書き込み方法（Arduino IDE）

1. ボードマネージャ URL に
   `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`
   を追加し、「esp32 by M5Stack」をインストール
2. ライブラリマネージャで **M5Unified** と **M5GFX** をインストール
3. ボードに **M5StopWatch** を選択（PSRAM は **OPI** が必須です）
4. `CatbotWalk/CatbotWalk.ino` を開いて書き込み

arduino-cli の場合:

```
arduino-cli compile --fqbn m5stack:esp32:m5stack_stopwatch:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc CatbotWalk
arduino-cli upload  --fqbn m5stack:esp32:m5stack_stopwatch:PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc -p <PORT> CatbotWalk
```

## ファイル

- `CatbotWalk/CatbotWalk.ino` — 本体
- `CatbotWalk/catbot_sprites.h` — 生成済みスプライト（RGB565＋α、8方向）
- `render_sprites.py` — スプライト再生成スクリプト（要: trimesh, numpy, Pillow）
- `catbot.glb` / `Catbot_____1body.step` — 変換元モデル
