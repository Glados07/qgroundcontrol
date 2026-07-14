# Custom 翻译维护说明

本目录集中管理 Custom 构建的翻译文件，使其与 QGroundControl 上游翻译相互独立，
同时保持与主目录相仿的翻译文件组织方式。

- `custom.ts` 是源字符串模板，仅用于更新和参考，不会被编译到应用程序中。
- `custom_<locale>.ts` 是各语言的翻译文件，CMake 会将其编译为 `.qm` 文件，
  并作为 `:/i18n` 下的资源打包到应用程序中。
- 生成的 `.qm` 文件属于构建产物，不应提交到源码仓库。

## 更新翻译文件

在 `custom/src` 或 `custom/res` 中新增、删除、移动或修改包含 `qsTr()` 的代码后，
运行以下脚本更新翻译文件：

```sh
custom/translations/custom-lupdate.sh
```

脚本默认从 `PATH` 中查找 `lupdate`。必要时可以通过 `LUPDATE` 指定其完整路径：

```sh
LUPDATE=/path/to/Qt/bin/lupdate custom/translations/custom-lupdate.sh
```

更新完成后，应使用 Qt Linguist 检查每个本地化翻译文件。需要特别检查因 QML
文件移动或拆分而被自动迁移到新翻译上下文的译文，并将复核无误的译文标记为完成。
带有 `type="unfinished"` 的条目可能不会写入最终生成的 `.qm` 文件。

正常构建应用程序即可编译各语言翻译文件。Custom 插件会根据当前语言环境，
加载以 `custom_` 为前缀的对应翻译资源。
