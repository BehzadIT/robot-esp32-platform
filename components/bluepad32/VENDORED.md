# Bluepad32 Vendoring Note

- Upstream project: `https://github.com/ricardoquesada/bluepad32`
- Vendored commit: `e9b755faabc240585da42e6d26164bb2cdd064d3`
- This repo carries the ESP-IDF custom-platform integration adjustments proven
  during Phase 1A so the component builds as a project-local dependency.

Key local integration differences:
- custom-platform builds do not compile the original ESP32 platform targets
- console-only dependencies stay gated behind the USB console option
- this component is paired with the repo-local vendored `btstack` component
