/***************************************************************************
    copyright            : (C) 2002 - 2008 by Scott Wheeler
    email                : wheeler@kde.org
 ***************************************************************************/

/***************************************************************************
 *   This library is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License version   *
 *   2.1 as published by the Free Software Foundation.                     *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful, but   *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA         *
 *   02110-1301  USA                                                       *
 *                                                                         *
 *   Alternatively, this file is available under the Mozilla Public        *
 *   License Version 1.1.  You may obtain a copy of the License at         *
 *   http://www.mozilla.org/MPL/                                           *
 ***************************************************************************/

#include "tfile.h"
#include "tfilestream.h"
#include "tstring.h"
#include "tdebug.h"
#include "tpropertymap.h"

#ifdef _WIN32
# include <windows.h>
# include <io.h>
#else
# include <stdio.h>
# include <unistd.h>
#endif

#ifndef R_OK
# define R_OK 4
#endif
#ifndef W_OK
# define W_OK 2
#endif

using namespace TagLib;

class TagLib::File::FilePrivate
{
public:
  FilePrivate(IOStream *stream, bool owner) :
    stream(stream),
    streamOwner(owner),
    valid(true) {}

  ~FilePrivate()
  {
    if(streamOwner)
      delete stream;
  }

  IOStream *stream;
  bool streamOwner;
  bool valid;
};

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

TagLib::File::File(FileName fileName) :
  d(new FilePrivate(new FileStream(fileName), true))
{
}

TagLib::File::File(IOStream *stream) :
  d(new FilePrivate(stream, false))
{
}

TagLib::File::~File()
{
  delete d;
}

FileName TagLib::File::name() const
{
  return d->stream->name();
}

ByteVector TagLib::File::readBlock(unsigned long length)
{
  return d->stream->readBlock(length);
}

void TagLib::File::writeBlock(const ByteVector &data)
{
  d->stream->writeBlock(data);
}

long TagLib::File::find(const ByteVector &pattern, long fromOffset, const ByteVector &before)
{
  if(!d->stream || pattern.size() > bufferSize())
      return -1;

  // The position in the file that the current buffer starts at.

  long bufferOffset = fromOffset;
  ByteVector buffer;

  // These variables are used to keep track of a partial match that happens at
  // the end of a buffer.

  int previousPartialMatch = -1;
  int beforePreviousPartialMatch = -1;

  // Save the location of the current read pointer.  We will restore the
  // position using seek() before all returns.

  long originalPosition = tell();

  // Start the search at the offset.

  seek(fromOffset);

  // This loop is the crux of the find method.  There are three cases that we
  // want to account for:
  //
  // (1) The previously searched buffer contained a partial match of the search
  // pattern and we want to see if the next one starts with the remainder of
  // that pattern.
  //
  // (2) The search pattern is wholly contained within the current buffer.
  //
  // (3) The current buffer ends with a partial match of the pattern.  We will
  // note this for use in the next iteration, where we will check for the rest
  // of the pattern.
  //
  // All three of these are done in two steps.  First we check for the pattern
  // and do things appropriately if a match (or partial match) is found.  We
  // then check for "before".  The order is important because it gives priority
  // to "real" matches.

  for(buffer = readBlock(bufferSize()); buffer.size() > 0; buffer = readBlock(bufferSize())) {

    // (1) previous partial match

    if(previousPartialMatch >= 0 && int(bufferSize()) > previousPartialMatch) {
      const int patternOffset = (bufferSize() - previousPartialMatch);
      if(buffer.containsAt(pattern, 0, patternOffset)) {
        seek(originalPosition);
        return bufferOffset - bufferSize() + previousPartialMatch;
      }
    }

    if(!before.isEmpty() && beforePreviousPartialMatch >= 0 && int(bufferSize()) > beforePreviousPartialMatch) {
      const int beforeOffset = (bufferSize() - beforePreviousPartialMatch);
      if(buffer.containsAt(before, 0, beforeOffset)) {
        seek(originalPosition);
        return -1;
      }
    }

    // (2) pattern contained in current buffer

    long location = buffer.find(pattern);
    if(location >= 0) {
      seek(originalPosition);
      return bufferOffset + location;
    }

    if(!before.isEmpty() && buffer.find(before) >= 0) {
      seek(originalPosition);
      return -1;
    }

    // (3) partial match

    previousPartialMatch = buffer.endsWithPartialMatch(pattern);

    if(!before.isEmpty())
      beforePreviousPartialMatch = buffer.endsWithPartialMatch(before);

    bufferOffset += bufferSize();
  }

  // Since we hit the end of the file, reset the status before continuing.

  clear();

  seek(originalPosition);

  return -1;
}


long TagLib::File::rfind(const ByteVector &pattern, long fromOffset, const ByteVector &before)
{
  if(!d->stream || pattern.size() > bufferSize())
      return -1;

  // The position in the file that the current buffer starts at.

  ByteVector buffer;

  // These variables are used to keep track of a partial match that happens at
  // the end of a buffer.

  /*
  int previousPartialMatch = -1;
  int beforePreviousPartialMatch = -1;
  */

  // Save the location of the current read pointer.  We will restore the
  // position using seek() before all returns.

  long originalPosition = tell();

  // Start the search at the offset.

  if(fromOffset == 0)
    fromOffset = length();

  long bufferLength = bufferSize();
  long bufferOffset = fromOffset + pattern.size();

  // See the notes in find() for an explanation of this algorithm.

  while(true) {

    if(bufferOffset > bufferLength) {
      bufferOffset -= bufferLength;
    }
    else {
      bufferLength = bufferOffset;
      bufferOffset = 0;
    }
    seek(bufferOffset);

    buffer = readBlock(bufferLength);
    if(buffer.isEmpty())
      break;

    // TODO: (1) previous partial match

    // (2) pattern contained in current buffer

    const long location = buffer.rfind(pattern);
    if(location >= 0) {
      seek(originalPosition);
      return bufferOffset + location;
    }

    if(!before.isEmpty() && buffer.find(before) >= 0) {
      seek(originalPosition);
      return -1;
    }

    // TODO: (3) partial match
  }

  // Since we hit the end of the file, reset the status before continuing.

  clear();

  seek(originalPosition);

  return -1;
}

void TagLib::File::insert(const ByteVector &data, unsigned long start, unsigned long replace)
{
  d->stream->insert(data, start, replace);
}

void TagLib::File::removeBlock(unsigned long start, unsigned long length)
{
  d->stream->removeBlock(start, length);
}

bool TagLib::File::readOnly() const
{
  return d->stream->readOnly();
}

bool TagLib::File::isOpen() const
{
  return d->stream->isOpen();
}

bool TagLib::File::isValid() const
{
  return isOpen() && d->valid;
}

void TagLib::File::seek(long offset, Position p)
{
  d->stream->seek(offset, IOStream::Position(p));
}

void TagLib::File::truncate(long length)
{
  d->stream->truncate(length);
}

void TagLib::File::clear()
{
  d->stream->clear();
}

long TagLib::File::tell() const
{
  return d->stream->tell();
}

long TagLib::File::length()
{
  return d->stream->length();
}

bool TagLib::File::isReadable(const char *file)
{

#if defined(_MSC_VER) && (_MSC_VER >= 1400)  // VC++2005 or later

  return _access_s(file, R_OK) == 0;

#else

  return access(file, R_OK) == 0;

#endif

}

bool TagLib::File::isWritable(const char *file)
{

#if defined(_MSC_VER) && (_MSC_VER >= 1400)  // VC++2005 or later

  return _access_s(file, W_OK) == 0;

#else

  return access(file, W_OK) == 0;

#endif

}

////////////////////////////////////////////////////////////////////////////////
// protected members
////////////////////////////////////////////////////////////////////////////////

unsigned int TagLib::File::bufferSize()
{
  return 1024;
}

void TagLib::File::setValid(bool valid)
{
  d->valid = valid;
}

