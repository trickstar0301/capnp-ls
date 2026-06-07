build_dir := env_var_or_default("CAPNP_LS_BUILD_DIR", "build")
capnp_source_dir := env_var_or_default("CAPNP_SOURCE_DIR", "")

test:
    if [ ! -f "{{build_dir}}/CMakeCache.txt" ]; then \
      if [ -z "{{capnp_source_dir}}" ]; then \
        echo "Set CAPNP_SOURCE_DIR to configure {{build_dir}}."; \
        exit 2; \
      fi; \
      cmake -B "{{build_dir}}" -DCAPNP_SOURCE_DIR="{{capnp_source_dir}}" .; \
    fi
    cmake --build "{{build_dir}}"
    ctest --test-dir "{{build_dir}}" --output-on-failure
    sh -n install.sh
    sh tests/install_sh_test.sh

test-install:
    sh -n install.sh
    sh tests/install_sh_test.sh
