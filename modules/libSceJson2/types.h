#pragma once
#include "codes.h"

namespace sce {
namespace Json {
class MemAllocator {
  public:
  MemAllocator();
  virtual ~MemAllocator();
  virtual void* allocate(size_t size, void* userData) = 0;
  virtual void  deallocate(void* ptr, void* userData) = 0;
  virtual void  notifyError(int32_t error, size_t size, void* userData);
};

enum ValueType { Null = 0, Boolean, Integer, UnsignedInteger, Double, String, Array, Object };

enum SpecialFloatFormat { FloatString, FloatNull, FloatSym };

class InitParameter {
  public:
  InitParameter(): m_alloc(nullptr), m_userData(nullptr), m_fileBuffSize(0) {}

  InitParameter(MemAllocator* alloc, void* ud, size_t sz): m_alloc(alloc), m_userData(ud), m_fileBuffSize(sz) {}

  private:
  MemAllocator* m_alloc;
  void*         m_userData;
  size_t        m_fileBuffSize;
};

class InitParameter2 {
  public:
  InitParameter2();

  void setAllocator(MemAllocator* alloc, void* userData);
  void setFileBufferSize(size_t size);
  void setSpecialFloatFormatType(SpecialFloatFormat fmt);

  MemAllocator*      getAllocator() const;
  void*              getUserData() const;
  size_t             getFileBufferSize() const;
  SpecialFloatFormat getSpecialFloatFormatType() const;

  private:
  MemAllocator*      m_alloc;
  void*              m_userData;
  size_t             m_fileBuffSize;
  SpecialFloatFormat m_floatFormat;
  uint32_t           m_padding[3];
};

class Initializer {
  public:
  Initializer();
  ~Initializer();

  int32_t initialize(const InitParameter*);
  int32_t initialize(const InitParameter2*);
  int32_t terminate();
  int32_t setGlobalTypeMismatchHandler(void* /* todo func typedef */ func, void* context);
  int32_t setGlobalNullAccessCallback(void* /* todo func typedef */ func, void* context);
  int32_t setGlobalSpecialFloatHandler(void* /* todo func typedef */ func, void* context);
  int32_t setGlobalElementAccessFailureHandler(void* /* todo func typedef */ func, void* context);

  typedef void   SYSV_ABI (*AllocInfoCallback)(int32_t info, int32_t accesstype, void* context);
  static int32_t setAllocatorInfoCallback(AllocInfoCallback func, void* context);

  private:
  bool m_bIsInited;
};
} // namespace Json
} // namespace sce
