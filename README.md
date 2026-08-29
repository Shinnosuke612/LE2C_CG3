[![DebugBuild](https://github.com/Shinnosuke612/LE2C_CG3/actions/workflows/DebugBuild.yml/badge.svg)](https://github.com/Shinnosuke612/LE2C_CG3/actions/workflows/DebugBuild.yml)

# ツール概要

自作C++／DirectX 12エンジン上に、SceneとPrefabを編集するゲームエディターを開発した。

モデルやPrefabをSceneへ配置するだけでなく、Prefab専用の編集画面、アニメーションTimeline、Instance差分のApply／Revert、Nested Prefab、Prefab Variant、Asset参照の検証までを一つの制作フローとして実装している。

# 加点項目

課題資料に記載された項目のうち、次の機能を実装した。

* ローダーと配置
* コライダーをゲーム側の当たり判定に適用
* 無効フラグ
* SpawnPoint相当のEnemySpawner配置
* EventTriggerの配置
* カメラの配置

# 独自実装

* Prefab専用の編集セッション
    * Sceneとは別のDocumentとしてPrefabを開き、Save、Reload、Closeを管理する
    * Dirty状態とUndo／Redo履歴をSceneから分離する
    * DirtyなPrefabを切り替える場合はSave／Discard／Cancelを選択できる
* Prefab Stage
    * Prefabの階層全体を専用Render TargetとOrbit CameraでPreviewする
    * Prefab Hierarchy、Prefab Inspector、Move／Rotate／Scale Gizmoを同じ画面で操作する
    * Local／World、Snap、Orbit、Zoom、Frame Allに対応する
    * Gizmoのドラッグ操作を一回分のUndoとして記録する
* 制作用の可視化と選択
    * Skeleton、Joint Axes、Collider、HitBox／HurtBoxをPrefab Stageへ重ねて表示する
    * 非Active EntityのColliderも編集用表示に含める
    * Colliderだけを対象にしたPickingへ切り替えられる
* PrefabAnimator Timeline
    * Play／Pause／Stop／SeekしながらPrefabアニメーションを確認する
    * TransformのPosition、Rotation、ScaleキーをPose単位にまとめて編集する
    * Poseの追加、複製、削除、不足キーの補完に対応する
    * 現在PoseをGizmoで直接編集し、EasingとPosition Bulge Offsetも設定できる
    * HitBox／HurtBoxのActive区間をTimeline上で確認する
* Prefabへのアクセスと配置
    * Project、Hierarchy、Inspector、Quick Openから共通のOpen処理でPrefabを開く
    * Quick Openに検索、Recent、Favorites、Prefabs Onlyを用意する
    * BreadcrumbとBack／ForwardでPrefab間を移動する
    * ProjectからScene ViewまたはHierarchyへDrag & Dropし、Linked Instanceとして配置する
* Property Override
    * Scene上で変更したPrefab Instanceの差分を収集する
    * Property、Component、Entity Branch、Instance単位でApply／Revertする
    * EntityLocalId、ComponentLocalId、PropertyPathを使い、配列内要素を含む差分を識別する
* Asset IDと参照移行
    * PrefabへUUID Asset IDを付与し、現在Pathとの対応をRegistryで管理する
    * 旧Path参照をAsset ID＋Fallback Path形式へ移行する
    * 未設定、重複、解決不能なAsset IDをEditor上で検出する
* Nested Prefab
    * Prefab内へ別Prefabを配置し、複数のPrefab境界を維持する
    * 直接・間接の循環参照を拒否する
    * Apply／Revert／Unpack時もNested PrefabのLinkを維持する
    * 複数のPrefab Sourceがある場合はApply先を選択できる
* Prefab Variant
    * Base PrefabからVariantを作成し、差分だけを保存する
    * Revert to Base、Apply to Base、Base更新後のRebaseに対応する
    * Variant内の個別PropertyもApply／Revertできる
* Diagnosticsとデータ保護
    * Current Prefabと全Prefabを対象にValidationを実行する
    * Load Error、Migration Required、Missing Reference、Duplicate Asset ID、Cycleを表示する
    * Scene JSONのMigration、Validation、Backup Recoveryを実装する
* Editor用Component
    * Camera／CameraPath、EventTrigger、EnemySpawner、Collider、PostProcessProfileManager、TextRendererなどをInspectorから設定する
    * Editorで保存したComponentデータをRuntime側のSystemへ反映する

# 制作フロー

1. ProjectまたはQuick Openから編集するPrefabを開く。
2. Prefab HierarchyとInspectorでEntity、Component、Transformを編集する。
3. Prefab Stageで見た目、Skeleton、Collider、HitBoxを確認する。
4. 必要に応じてPrefabAnimator TimelineでPoseとActive区間を編集する。
5. Prefabを保存し、ProjectからSceneへDrag & Dropして配置する。
6. Scene固有の変更はOverrideとして保持し、必要な差分だけApplyまたはRevertする。
7. DiagnosticsでAsset ID、Nested参照、Migration状態を確認してから保存する。

# 補足

PostEffect Profileの編集、EventTriggerからのProfile切り替え、DissolveのRuntime再生などもEditor用Componentの拡張例として実装している。ただし本資料では、ゲーム制作を支える基盤としてPrefab編集ワークフローを中心に紹介する。
