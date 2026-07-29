[![DebugBuild](https://github.com/Shinnosuke612/LE2C_CG3/actions/workflows/DebugBuild.yml/badge.svg)](https://github.com/Shinnosuke612/LE2C_CG3/actions/workflows/DebugBuild.yml)

# PostEffect実装資料

## 概要

Editorの`Post Process Stack`では、Scene標準設定とPost Process Profileの両方から各効果の有効・無効とパラメータを編集できる。提出用Sceneには、課題項目を実行中に確認するためのProfile切替も組み込んでいる。

## 課題項目との対応

| 課題項目 | 点数 | 実装内容 |
| --- | ---: | --- |
| Grayscale | 必須 | RGBを輝度へ変換して画面全体をモノクロ化 |
| Vignetting | 3 | `Vignette`として実装 |
| BoxFilter | 3 | `Box Blur`として3×3／5×5 Kernelに対応 |
| GaussianFilter | 5 | `Gaussian Blur`としてKernel、Sigma、Strengthを設定可能 |
| LuminanceBasedOutline | 5 | 輝度差によるOutline検出として実装 |
| DepthBasedOutline | 8 | Depth差によるOutline検出として実装 |
| Radial Blur | 5 | Center、Width、Samplesを設定可能 |
| Dissolve | 4 | Noise Mask、Threshold、Edge表現とRuntime時間遷移を実装 |
| Random | 4 | `Noise`として静的／時間変化Noiseを実装 |
| その他 | 最大20 | Bloom、Depth of Field、Camera Motion Blur、Pixelation、Chromatic Aberrationを実装 |

## 提出Sceneでの確認方法

Release実行後、`P`キーを1回押すごとにPostEffectが次の順番で切り替わる。

```text
None
→ Grayscale
→ Vignette
→ Bloom
→ Box Blur
→ Gaussian Blur
→ Depth of Field
→ Camera Motion Blur
→ Radial Blur
→ Noise
→ Dissolve
→ Outline
→ Pixelation
→ Chromatic Aberration
→ None
```

- 左上に`Pキーで切り替え`を固定表示する。
- 右上に`PostEffect: <現在のProfile名>`を表示する。
- 各Profileでは、確認対象のPostEffectだけを有効にする。
- `P`キーは押した瞬間だけ反応し、押し続けで連続切替しない。
- 最後のProfileの次は`None`へ戻る。

## 実装済みPostEffect

### Grayscale

画面のRGBを輝度へ変換し、モノクロ表示する。課題の必須項目に対応する。

### Vignette

画面端を暗くする。Scale、Power、Intensityを設定できる。

### Bloom

高輝度部分を抽出してぼかし、元の画面へ加算する。提出Sceneでは単独Profileとして確認できる。

### Box Blur

周囲の画素を均等に平均化する。3×3／5×5 KernelとStrengthを設定できる。

### Gaussian Blur

中心に近い画素ほど大きな重みを与えてぼかす。3×3／5×5 Kernel、Sigma、Strengthを設定できる。

### Depth of Field

Depth Bufferを利用し、指定距離付近へ焦点を合わせる。Focus Distance、Focus Range、Near／Far Strength、Max Radiusを設定できる。

### Camera Motion Blur

現在Frameと前FrameのViewProjectionを利用し、カメラ移動による残像を生成する。物体単体のVelocity Blurは対象外。

### Radial Blur

指定したCenterから放射状に画面をぼかす。Blur WidthとSamplesを設定できる。

### Noise

画面へNoiseを加える。Amount、Scale、Speed、Seedと時間アニメーションの有無を設定できる。

### Dissolve

Noise MaskとThresholdを利用して画面を段階的に消去し、境界へ色を付ける。

提出用Dissolve Profileでは、選択後2秒間でThresholdが0から1へLinear変化する。再選択時は0から再生し、終了後は1を保持する。Pause中は進行せず、Reset、Scene Reload、Scene TransitionでRuntime状態を破棄する。

### Outline

輝度差またはDepth差から輪郭を検出する。両方の入力、Weight、Threshold、Softness、Thickness、Colorを設定できる。

### Pixelation

画面を指定Block Size単位へ分割し、低解像度風に表示する。

### Chromatic Aberration

画面中心からの距離に応じてRGBチャンネルをずらす。Center、Intensity、Falloffを設定できる。

## Editorでの設定

1. 対象Sceneを開く。
2. `Post Process Stack`でScene標準設定を編集する。
3. `PostProcessProfileManager`でProfileごとの設定を編集する。
4. 必要なPostEffectを有効にしてパラメータを調整する。
5. Sceneを保存する。

複数の効果を有効にした場合は、既定のPostEffect描画順に従って順番に適用される。提出用Profileは比較しやすいよう、一つの効果だけを有効にしている。

## Runtime実装

- `EventTrigger(OnKeyPressed: P)`から`NextPostProcessProfile`を要求する。
- `ScenePostProcessProfileSystem`が選択Profileと実行時PostProcess設定を保持する。
- Dissolveの経過時間とThresholdはRuntime Copyだけへ適用し、Scene JSONを毎Frame変更しない。
- 現在のProfile名はTextRendererのRuntime文字列として更新する。
- HUD文字はPostEffect適用後に描画するため、GrayscaleやBlurなどの影響を受けない。

## 補足実装

`Underwater`と`Water Refraction`のShader、設定、Editor項目、描画Passも実装している。ただし、現在の提出Sceneでは効果を明確に確認できないため、Pキーの提出用巡回には含めていない。Enterでシーン遷移が行えるため確認は可能。
