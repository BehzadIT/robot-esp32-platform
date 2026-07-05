# BTstack Vendoring Note

- Upstream submodule source:
  `https://github.com/ricardoquesada/bluepad32/tree/e9b755faabc240585da42e6d26164bb2cdd064d3/external/btstack`
- Upstream BTstack commit used during Phase 1A and 1B integration:
  `5d4d8cc7b1d35a90bbd6d5ffd2d3050b2bfc861c`
- Vendored form: the ESP-IDF component layout produced by Bluepad32's ESP32
  integration flow, copied into this repo so builds do not depend on patching
  the local ESP-IDF installation.
