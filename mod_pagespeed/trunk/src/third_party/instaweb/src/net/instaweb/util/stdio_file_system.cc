// Copyright 2010 and onwards Google Inc.
// Author: jmarantz@google.com (Joshua Marantz)

#include "net/instaweb/util/public/stdio_file_system.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include "net/instaweb/util/public/message_handler.h"
#include <string>

namespace net_instaweb {

namespace {

// Helper class to factor out common implementation details between Input and
// Output files, in lieu of multiple inheritance.
class FileHelper {
 public:
  FileHelper(FILE* f, const char* filename)
      : file_(f),
        filename_(filename),
        line_(1) {
  }

  ~FileHelper() {
    assert(file_ == NULL);
  }

  void CountNewlines(const char* buf, int size) {
    for (int i = 0; i < size; ++i, ++buf) {
      line_ += (*buf == '\n');
    }
  }

  void ReportError(MessageHandler* message_handler, const char* format) {
    message_handler->Error(filename_.c_str(), line_, format, strerror(errno));
  }

  bool Close(MessageHandler* message_handler) {
    bool ret = true;
    if (fclose(file_) != 0) {
      ReportError(message_handler, "closing file: %s");
      ret = false;
    }
    file_ = NULL;
    return ret;
  }

  FILE* file_;
  std::string filename_;
  int line_;
};

}  // namespace

class StdioInputFile : public FileSystem::InputFile {
 public:
  StdioInputFile(FILE* f, const char* filename) : file_helper_(f, filename) {
  }

  virtual int Read(char* buf, int size, MessageHandler* message_handler) {
    int ret = fread(buf, 1, size, file_helper_.file_);
    file_helper_.CountNewlines(buf, ret);
    if ((ret == 0) && (ferror(file_helper_.file_) != 0)) {
      file_helper_.ReportError(message_handler, "reading file: %s");
    }
    return ret;
  }

  virtual bool Close(MessageHandler* message_handler) {
    return file_helper_.Close(message_handler);
  }

  virtual const char* filename() { return file_helper_.filename_.c_str(); }

 private:
  FileHelper file_helper_;
};

class StdioOutputFile : public FileSystem::OutputFile {
 public:
  StdioOutputFile(FILE* f, const char* filename) : file_helper_(f, filename) {
  }

  virtual int Write(
      const char* buf, int size, MessageHandler* message_handler) {
    int ret = fwrite(buf, 1, size, file_helper_.file_);
    file_helper_.CountNewlines(buf, ret);
    if (ret != size) {
      file_helper_.ReportError(message_handler, "writing file: %s");
    }
    return ret;
  }

  virtual bool Flush(MessageHandler* message_handler) {
    bool ret = true;
    if (fflush(file_helper_.file_) != 0) {
      file_helper_.ReportError(message_handler, "flushing file: %s");
      ret = false;
    }
    return ret;
  }

  virtual bool Close(MessageHandler* message_handler) {
    return file_helper_.Close(message_handler);
  }

  virtual const char* filename() { return file_helper_.filename_.c_str(); }

  virtual bool SetWorldReadable(MessageHandler* message_handler) {
    bool ret = true;
    int fd = fileno(file_helper_.file_);
    int status = fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (status != 0) {
      ret = false;
      file_helper_.ReportError(message_handler, "setting world-readble: %s");
    }
    return ret;
  }

 private:
  FileHelper file_helper_;
};

StdioFileSystem::~StdioFileSystem() {
}

FileSystem::InputFile* StdioFileSystem::OpenInputFile(
    const char* filename, MessageHandler* message_handler) {
  FileSystem::InputFile* input_file = NULL;
  FILE* f = fopen(filename, "r");
  if (f == NULL) {
    message_handler->Error(filename, 0, "opening input file: %s",
                           strerror(errno));
  } else {
    input_file = new StdioInputFile(f, filename);
  }
  return input_file;
}


FileSystem::OutputFile* StdioFileSystem::OpenOutputFile(
    const char* filename, MessageHandler* message_handler) {
  FileSystem::OutputFile* output_file = NULL;
  if (strcmp(filename, "-") == 0) {
    output_file = new StdioOutputFile(stdout, "<stdout>");
  } else {
    FILE* f = fopen(filename, "w");
    if (f == NULL) {
      message_handler->Error(filename, 0,
                             "opening output file: %s", strerror(errno));
    } else {
      output_file = new StdioOutputFile(f, filename);
    }
  }
  return output_file;
}

FileSystem::OutputFile* StdioFileSystem::OpenTempFile(
    const char* prefix, MessageHandler* message_handler) {
  // TODO(jmarantz): As jmaessen points out, mkstemp warns "Don't use
  // this function, use tmpfile(3) instead.  It is better defined and
  // more portable."  However, tmpfile does not allow a location to be
  // specified.  I'm not 100% sure if that's going to be work well for
  // us.  More importantly, our usage scenario is that we will be
  // closing the file and renaming it to a permanent name.  tmpfiles
  // automatically are deleted when they are closed.
  int prefix_len = strlen(prefix);
  static char mkstemp_hook[] = "XXXXXX";
  char* template_name = new char[prefix_len + sizeof(mkstemp_hook)];
  memcpy(template_name, prefix, prefix_len);
  memcpy(template_name + prefix_len, mkstemp_hook, sizeof(mkstemp_hook));
  int fd = mkstemp(template_name);
  OutputFile* output_file = NULL;
  if (fd < 0) {
    message_handler->Error(template_name, 0,
                           "opening temp file: %s", strerror(errno));
  } else {
    FILE* f = fdopen(fd, "w");
    if (f == NULL) {
      close(fd);
      message_handler->Error(template_name, 0,
                             "re-opening temp file: %s", strerror(errno));
    } else {
      output_file = new StdioOutputFile(f, template_name);
    }
  }
  delete [] template_name;
  return output_file;
}

bool StdioFileSystem::RenameFile(const char* old_file, const char* new_file,
                                 MessageHandler* message_handler) {
  bool ret = true;
  if (rename(old_file, new_file) < 0) {
    message_handler->Error(old_file, 0, "renaming file to %s: %s",
                           new_file, strerror(errno));
    ret = false;
  }
  return ret;
}

}  // namespace net_instaweb
