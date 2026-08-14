# Sekai Deck Recommendation C++

A C++ optimized version of [sekai-calculator](https://github.com/xfl03/sekai-calculator) with Python bindings, providing both the original brute-force search algorithm and some new randomized algorithms.

## Install 

This fork’s PyPI name is **`sekai-deck-recommend-cpp-resona`** 
Import name is unchanged: `sekai_deck_recommend_cpp`.

After a successful `v*` release:

```bash
uv pip install sekai-deck-recommend-cpp-resona
```

### Upstream package

```bash
uv pip install sekai-deck-recommend-cpp
```

## Install from source

### Prerequisites

- CMake ≥ 3.15
- C++20 compatible compiler (GCC/Clang/MSVC)
- Python 3.10+ with development headers

### Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/Resonalofi/sekai-deck-recommend-cpp.git
cd sekai-deck-recommend-cpp

# Install via pip (compiles locally)
uv pip install -e . -v
# or: pip install -e . -v
```

## Build for WebAssembly

Install and activate the Emscripten SDK, then configure the browser build with Ninja:

```bash
emcmake cmake -S . -B build/wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR=/absolute/path/to/browser/assets
cmake --build build/wasm -j 4
```

The output directory receives `sekai_deck_recommend_wasm.js`,
`sekai_deck_recommend_wasm.wasm`, and `sekai_deck_recommend_wasm.data`.
Omit `SEKAI_DECK_RECOMMEND_WASM_OUTPUT_DIR` to keep the files in the CMake build directory.

Tagged releases and manual runs of the `Build and publish WASM to R2` workflow
build these assets and synchronize them to a dedicated Cloudflare R2 prefix. Configure
the following GitHub Actions repository secrets:

- `R2_ENDPOINT`: the complete R2 S3 API endpoint
- `R2_ACCESS_KEY_ID`: the R2 API token access key ID
- `R2_SECRET_ACCESS_KEY`: the R2 API token secret access key
- `R2_BUCKET_NAME`: the destination bucket
- `R2_WASM_PREFIX`: a non-empty prefix reserved for the current WASM assets

The workflow removes remote files under `R2_WASM_PREFIX` that are not present in the
new build. Do not point this secret at the bucket root or a shared prefix.

## Build as a native C++ library

The native target is independent from pybind11 and does not change the default wheel build:

```bash
cmake -S . -B build/native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSEKAI_DECK_RECOMMEND_BUILD_PYTHON=OFF \
  -DSEKAI_DECK_RECOMMEND_BUILD_NATIVE=ON
cmake --build build/native --parallel
```

When this project is included with `add_subdirectory`, link
`SekaiDeckRecommend::Core` and include `sekai_deck_recommend/native.h`.
`NativeEngine` accepts nlohmann JSON options, including `user_data` as an object.
Copies share parsed master data and music metadata; `recommend` is a const read operation,
so immutable engine snapshots can serve concurrent callers.

The native target only exposes recommendation-domain behavior. HTTP, authentication,
remote fetching, cache generations, and deployment remain responsibilities of the consuming
service.

## Usage

```python
from sekai_deck_recommend_cpp import (
    SekaiDeckRecommend, 
    DeckRecommendOptions,
    DeckRecommendCardConfig
)
   
sekai_deck_recommend = SekaiDeckRecommend()

sekai_deck_recommend.update_masterdata("base/dir/of/masterdata", "jp")
sekai_deck_recommend.update_musicmetas("file/path/of/musicmetas.json", "jp")

options = DeckRecommendOptions()

# optimizing target in ["score", "power", "skill", "bonus"], default is "score"
options.target = "score"

# "ga" for genetic algorithm, "dfs" for brute-force search
# default is "ga"
options.algorithm = "ga"   

options.region = "jp"
options.user_data_file_path = "user/data/file/path.json"
options.live_type = "multi"
options.music_id = 74
options.music_diff = "expert"
options.event_id = 160

result = sekai_deck_recommend.recommend(options)
```

For more details of options, please refer the docstring of `sekai_deck_recommend.DeckRecommendOptions`

## Acknowledgments
- Original implementation by [xfl03/sekai-calculator](https://github.com/xfl03/sekai-calculator)
- JSON parsing by [nlohmann/json](https://github.com/nlohmann/json)
- Python bindings powered by [pybind11](https://github.com/pybind/pybind11)
