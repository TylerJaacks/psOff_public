#pragma once

#include "utility/utility.h"

#include <cstdint>
#include <functional>
#include <string>

typedef std::function<void(const void*)> vvpfunc;

class ITrophies {
  CLASS_NO_COPY(ITrophies);
  CLASS_NO_MOVE(ITrophies);

  protected:
  ITrophies() = default;

  public:
  virtual ~ITrophies() = default;

  enum class ParserErr {
    SUCCESS = 0,        // No errors, we're fine
    INVALID_CONTEXT,    // Context is nullptr
    OUT_OF_MEMORY,      // Failed to allocate data array
    CONTINUE,           // Not an actual error code. For internal usage only!
    NO_KEY_SET,         // No root key installed
    INVALID_MAGIC,      // TRP file has invalid magic in its header
    INVALID_VERSION,    // TRP file version is not valid
    INVALID_ENTSIZE,    // TRP file has bigger entries
    INVALID_AES,        // TRP file contains unaligned AES blocks
    INVALID_XML,        // TPR file contains invalid XML data, can't parse it
    NOT_IMPLEMENTED,    // This feature is not implemented yet
    IO_FAIL,            // Failed to read TRP file
    NO_CALLBACKS,       // Parser called with no callbacks, it's pointless
    DECRYPT,            // TRP file decryption failed
    NO_TROPHIES,        // Failed to open TRP file or the said file does not contain any esfm file
    MAX_TROPHY_REACHED, // The game hit the hard limit of 128 trophies
  };

  struct usr_context {
    struct trophy {
      int32_t  id = -1;
      uint32_t re = 0; // reserved
      uint64_t ts = 0;
    };

    uint32_t label  = 0;
    int32_t  userId = -1;

    std::vector<trophy> trophies = {};
  };

  struct trp_grp_cb {
    struct data_t {
      int32_t     id;
      std::string name;
      std::string detail;
    } data;

    bool                         cancelled;
    std::function<bool(data_t*)> func;
  };

  struct trp_ent_cb {
    struct data_t {
      int32_t     id;
      int32_t     group;
      int32_t     platinum;
      bool        hidden;
      uint8_t     grade;
      std::string name;
      std::string detail;
    } data;

    bool                         cancelled;
    std::function<bool(data_t*)> func;
  };

  struct trp_png_cb {
    struct data_t {
      std::string pngname;
      void*       pngdata;
      size_t      pngsize;
    } data;

    bool                         cancelled;
    std::function<bool(data_t*)> func;
  };

  struct trp_inf_cb {
    struct data_t {
      std::string title_name;
      std::string title_detail;
      std::string trophyset_version;
      uint32_t    trophy_count;
      uint32_t    group_count;
    } data;

    bool                         cancelled;
    std::function<bool(data_t*)> func;
  };

  struct trp_context {
    bool lightweight;

    trp_ent_cb entry = {.data = {}, .cancelled = false, .func = nullptr};
    trp_grp_cb group = {.data = {}, .cancelled = false, .func = nullptr};
    trp_png_cb pngim = {.data = {}, .cancelled = false, .func = nullptr};
    trp_inf_cb itrop = {.data = {}, .cancelled = false, .func = nullptr};

    inline bool cancelled() { return entry.cancelled && group.cancelled && pngim.cancelled && itrop.cancelled; }
  };

  struct trp_unlock_data {
    int32_t  userId;
    uint32_t label;

    struct image {
      void*  pngdata;
      size_t pngsize;
    } image;

    int32_t id;
    int32_t platId;
    bool    platGained;
    uint8_t grade;

    std::string name;
    std::string descr;
    std::string pname;
    std::string pdescr;
  };

  virtual ParserErr   parseTRP(uint32_t label, trp_context* context) = 0;
  virtual const char* getError(ParserErr ec)                         = 0;

  //  Callbacks

  virtual void addTrophyUnlockCallback(vvpfunc func) = 0;

  //- Callbacks

  virtual int32_t      createContext(int32_t userId, uint32_t label, int32_t* ctxid)         = 0;
  virtual usr_context* getContext(int32_t ctxid)                                             = 0;
  virtual int32_t      destroyContext(usr_context* ctx)                                      = 0;
  virtual int32_t      getProgress(usr_context* ctx, uint32_t progress[4], uint32_t* count)  = 0;
  virtual uint64_t     getUnlockTime(usr_context* ctx, int32_t trophyId)                     = 0;
  virtual int32_t      unlockTrophy(usr_context* ctx, int32_t trophyId, int32_t* platinumId) = 0;
  virtual int32_t      resetUserInfo(usr_context* ctx)                                       = 0;
};

#if defined(__APICALL_EXTERN)
#define __APICALL __declspec(dllexport)
#elif defined(__APICALL_IMPORT)
#define __APICALL __declspec(dllimport)
#else
#define __APICALL
#endif
__APICALL ITrophies& accessTrophies();
#undef __APICALL
