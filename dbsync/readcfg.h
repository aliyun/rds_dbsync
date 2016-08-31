#ifndef _READCFG_H_
#define _READCFG_H_

#include "postgres_fe.h"
#include "c.h"

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <string>

#include "ini.h"

using std::string;

#define uint64_t uint64

class Config {
   public:
    Config(const string& filename);
    ~Config();
    string Get(const string& sec, const string& key,
               const string& defaultvalue);
    bool Scan(const string& sec, const string& key, const char* scanfmt,
              void* dst);
    void* Handle() { return (void*)this->_conf; };

   private:
    ini_t* _conf;
};

bool to_bool(std::string str);

void find_replace(string& str, const string& find, const string& replace);

#endif  // _UTILFUNCTIONS_
