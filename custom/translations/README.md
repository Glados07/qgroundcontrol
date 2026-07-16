# Custom 翻译维护说明

本目录集中管理 Custom 构建的翻译文件，并与 QGroundControl 上游翻译保持独立。

- `custom.ts` 是源字符串模板，只用于更新和参考，不编译进应用程序。
- `custom_<locale>.ts` 是各语言目录，CMake 会把它们编译为 `.qm` 并打包到 `:/i18n`。
- 生成的 `.qm` 属于构建产物，不提交到仓库。

## 更新方法

在 `custom/src` 中新增、删除、移动或修改包含 `qsTr()` 的代码后运行：

```sh
custom/translations/custom-lupdate.sh
```

脚本默认从 `PATH` 查找 Qt 6 `lupdate`，也可以显式指定：

```sh
LUPDATE=/path/to/Qt/bin/lupdate custom/translations/custom-lupdate.sh
```

更新后使用 Qt Linguist 检查本地化文件，重点复核文件移动产生的上下文变化和
`type="unfinished"` 条目。CustomPlugin 会根据当前语言加载对应的 `custom_*.qm`。
