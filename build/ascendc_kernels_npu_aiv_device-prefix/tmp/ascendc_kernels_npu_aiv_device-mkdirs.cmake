# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/usr/local/Ascend/ascend-toolkit/latest/compiler/tikcpp/ascendc_kernel_cmake/device_project")
  file(MAKE_DIRECTORY "/usr/local/Ascend/ascend-toolkit/latest/compiler/tikcpp/ascendc_kernel_cmake/device_project")
endif()
file(MAKE_DIRECTORY
  "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/src/ascendc_kernels_npu_aiv_device-build"
  "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix"
  "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/tmp"
  "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/src/ascendc_kernels_npu_aiv_device-stamp"
  "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/src"
  "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/src/ascendc_kernels_npu_aiv_device-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/src/ascendc_kernels_npu_aiv_device-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/data/lzy/pipellm_acl/build/ascendc_kernels_npu_aiv_device-prefix/src/ascendc_kernels_npu_aiv_device-stamp${cfgdir}") # cfgdir has leading slash
endif()
