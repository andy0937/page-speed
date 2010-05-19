// Copyright 2010 Google Inc. All Rights Reserved.
// Author: jmaessen@google.com (Jan Maessen)

#ifndef NET_INSTAWEB_REWRITER_PUBLIC_IMAGE_H_
#define NET_INSTAWEB_REWRITER_PUBLIC_IMAGE_H_

#include "net/instaweb/rewriter/public/input_resource.h"
#include <string>
#include "net/instaweb/util/public/string_util.h"
struct IplImage;

namespace net_instaweb {

struct ContentType;
class FileSystem;
class MessageHandler;
class ResourceManager;
class Writer;

class Image {
 public:
  // Images that are in the process of being transformed are represented by an
  // Image.  This class encapsulates various operations that are sensitive to
  // the format of the compressed image file and of the image libraries we are
  // using.  In particular, the timing of compression and decompression
  // operations may be a bit unexpected, because we may do these operations
  // early in order to retrieve image metadata, or we may choose to skip them
  // entirely if we don't need them or don't understand how to do them.
  //
  // In future we may need to plumb this to other data sources or change how
  // metadata is retrieved; the object is to do so locally in this class without
  // disrupting any of its clients.

  Image(const InputResource& original_image,
        FileSystem* file_system,
        ResourceManager* manager,
        MessageHandler* handler);

  ~Image() {
    CleanOpenCV();
  }

  enum Type {
    IMAGE_UNKNOWN = 0,
    IMAGE_JPEG,
    IMAGE_PNG,
    IMAGE_GIF,
  };

  // Note that at the moment asking for image dimensions can be expensive as it
  // invokes an external library.  This method can fail (returning false) for
  // various reasons: we don't understand the image format (eg a gif), we can't
  // find the headers, the library doesn't support a particular encoding, etc.
  // In general, we deal with failure here by passing data through unaltered.
  bool Dimensions(int* width, int* height);

  int input_size() const {
    return original_contents().size();
  }

  int output_size() {
    int ret;
    if (output_valid_ || ComputeOutputContents()) {
      ret = output_contents_.size();
    } else {
      ret = input_size();
    }
    return ret;
  }

  Type image_type() {
    if (image_type_ == IMAGE_UNKNOWN) {
      ComputeImageType();
    }
    return image_type_;
  }

  bool ResizeTo(int width, int height);

  // Method that lets us bail out if a resize actually cost space!
  void UndoResize() {
    if (resized_) {
      CleanOpenCV();
      output_valid_ = false;
      image_type_ = IMAGE_UNKNOWN;
      resized_ = false;
    }
  }

  // Return image-appropriate content type, or NULL if no content type is known.
  // Result is a top-level const pointer.
  const ContentType* content_type();
  bool WriteTo(Writer* output_resource);
  std::string AsInlineData();
 private:
  FileSystem* file_system_;
  MessageHandler* handler_;
  const InputResource& original_image_;
  ResourceManager* manager_;
  Type image_type_;
  std::string output_contents_;
  bool output_valid_;
  std::string opencv_filename_;
  IplImage* opencv_image_;
  bool opencv_load_possible_;
  bool resized_;

  const std::string& original_contents() const {
    return original_image_.contents();
  }
  void ComputeImageType();
  bool LoadOpenCV();
  void CleanOpenCV();
  bool ComputeOutputContents();
};

// Given a name (file or url), see if it has the canonical extension
// corresponding to a particular content type.
const ContentType* NameExtensionToContentType(const StringPiece& name);

}  // namespace net_instaweb

#endif  // NET_INSTAWEB_REWRITER_PUBLIC_IMAGE_H_