#pragma once

#include <JuceHeader.h>
#include <cstddef>
#include <memory>
#include <vector>

namespace upic
{
    //================ the file a chooser returned (M17) ================
    //
    // On the desktop the chooser returns a local file. On Android it returns
    // a document-provider URL (content://...), which JUCE's getResults()
    // throws away - it keeps only the local files - so the old code got an
    // empty File and every picked file did nothing at all. These helpers
    // work with the URL itself: a local file is used in place, a provider
    // document is copied into (or out of) a local file.
    //
    // The desktop path is unchanged: a local URL goes straight to the file.

    // A picked file, ready to be read.
    struct PickedFile
    {
        PickedFile() = default;

        juce::File file;      // the local file: the picked one, or a copy of it
        bool isCopy = false;  // true when the content was copied into a temporary file
        juce::String name;    // the name to show in a message

        // the temporary copy is deleted when the picked file is released
        void release()
        {
            if (isCopy)
                file.deleteFile();
            file = juce::File();
            isCopy = false;
        }

        // a copy that is not released must not leave the temporary file behind
        ~PickedFile() { release(); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PickedFile)
    };

    // True when the URL is a plain local file - the desktop case. A
    // document-provider URL (Android's content://) is never treated as one:
    // it is read and written through the provider, so the name the user
    // chose in the picker stays the name of the document.
    inline bool isPlainFileURL (const juce::URL& url)
    {
        return url.getScheme() == "file";
    }

    // the name of a picked URL, for the messages
    inline juce::String pickedFileName (const juce::URL& url)
    {
        if (! url.isWellFormed()) return {};
        if (isPlainFileURL (url)) return url.getLocalFile().getFileName();
        return url.getFileName();   // on Android this resolves the provider's name
    }

    // the picked URL with its extension changed. A local file keeps the old
    // behaviour (the extension is forced); a provider document keeps the name
    // the user chose - its name is fixed when the document is created.
    inline juce::URL pickedURLWithExtension (const juce::URL& url, const juce::String& ext)
    {
        if (! url.isWellFormed()) return {};
        if (isPlainFileURL (url))
            return juce::URL (url.getLocalFile().withFileExtension (ext));
        return url;
    }

    // Opens a stream for a URL - a local file or a provider document
    // (Android). JUCE's URL reader knows the content:// scheme there.
    inline std::unique_ptr<juce::InputStream> openPickedStream (const juce::URL& url)
    {
        return url.createInputStream (juce::URL::InputStreamOptions (
                                          juce::URL::ParameterHandling::inAddress));
    }

    // Opens a stream for a provider document (Android). The document API is
    // used directly: JUCE's URL output-stream helper hands the platform a
    // null document when the URL is not a document URI, and the stream is
    // then made from that null. The URL reader is the fallback.
    inline std::unique_ptr<juce::InputStream> openProviderStream (const juce::URL& url)
    {
       #if JUCE_ANDROID
        const auto document = juce::AndroidDocument::fromDocument (url);
        if (document.hasValue())
            return document.createInputStream();
       #endif
        return openPickedStream (url);
    }

    inline std::unique_ptr<juce::OutputStream> openProviderOutput (const juce::URL& url)
    {
       #if JUCE_ANDROID
        const auto document = juce::AndroidDocument::fromDocument (url);
        if (document.hasValue())
            return document.createOutputStream();
       #endif
        return url.createOutputStream();
    }

    // the size of the copy buffer used when a file is copied in or out
    constexpr int kCopyBlockSize = 4096;

    // Reads a picked file into a local file. A provider document cannot
    // always be read as a stream that supports seeking (the decoders need
    // that), so the content is copied into a local file first. The copy is
    // bounded by maxBytes: a file over the cap is refused, not read. A local
    // file is used in place - nothing is copied.
    inline bool readPickedFile (const juce::URL& url, PickedFile& out,
                                juce::int64 maxBytes, juce::String& error)
    {
        out.release();   // a previous copy must not be left behind
        if (! url.isWellFormed())
        {
            error = "No file was chosen";
            return false;
        }

        out.name = pickedFileName (url);

        if (isPlainFileURL (url))
        {
            const auto f = url.getLocalFile();
            if (! f.existsAsFile())
            {
                error = "The file does not exist: " + f.getFileName();
                return false;
            }
            if (f.getSize() > maxBytes)
            {
                error = "The file is too large: " + f.getFileName();
                return false;
            }
            out.file = f;
            return true;
        }

        // a provider document (Android): copy it into a local temporary file
        auto in = openProviderStream (url);
        if (in == nullptr)
        {
            error = "The chosen file could not be opened";
            return false;
        }

        auto temp = juce::File::createTempFile (".upic");
        if (temp == juce::File())
        {
            error = "Could not make a temporary file";
            return false;
        }
        auto outStream = temp.createOutputStream();
        if (outStream == nullptr)
        {
            temp.deleteFile();
            error = "Could not make a temporary file";
            return false;
        }

        juce::HeapBlock<char> block (kCopyBlockSize);
        juce::int64 total = 0;
        for (;;)
        {
            const auto read = in->read (block.get(), kCopyBlockSize);
            if (read <= 0) break;
            total += read;
            if (total > maxBytes)
            {
                outStream.reset();
                temp.deleteFile();
                error = "The chosen file is too large";
                return false;
            }
            if (! outStream->write (block.get(), (size_t) read))
            {
                outStream.reset();
                temp.deleteFile();
                error = "Could not read the chosen file";
                return false;
            }
        }
        outStream.reset();

        out.file = temp;
        out.isCopy = true;
        return true;
    }

    // Writes bytes to a picked destination - a local file (through a
    // temporary sibling, so a failed write keeps the old file) or a provider
    // document (through its output stream).
    inline bool writePickedBytes (const juce::URL& url, const void* data, size_t numBytes,
                                  juce::String& error)
    {
        if (! url.isWellFormed())
        {
            error = "No destination was chosen";
            return false;
        }

        if (isPlainFileURL (url))
        {
            const auto f = url.getLocalFile();
            juce::TemporaryFile temp (f);
            {
                auto out = temp.getFile().createOutputStream();
                if (out == nullptr || ! out->write (data, numBytes))
                {
                    error = "Could not write " + f.getFileName();
                    return false;
                }
            }
            if (! temp.overwriteTargetFileWithTemporary())
            {
                error = "Could not write " + f.getFileName();
                return false;
            }
            return true;
        }

        // a provider document (Android)
        auto out = openProviderOutput (url);
        if (out == nullptr)
        {
            error = "The chosen destination could not be opened";
            return false;
        }
        if (! out->write (data, numBytes))
        {
            error = "Could not write to the chosen destination";
            return false;
        }
        out.reset();
        return true;
    }

    // Copies a local file into a picked destination. A local destination is
    // not touched here - the caller wrote it directly (through the atomic
    // temporary sibling).
    inline bool copyLocalFileToPicked (const juce::File& source, const juce::URL& url,
                                       juce::String& error)
    {
        if (! url.isWellFormed())
        {
            error = "No destination was chosen";
            return false;
        }
        if (isPlainFileURL (url))
            return true;   // the caller wrote it

        auto in = source.createInputStream();
        if (in == nullptr)
        {
            error = "Could not read " + source.getFileName();
            return false;
        }
        auto out = openProviderOutput (url);
        if (out == nullptr)
        {
            error = "The chosen destination could not be opened";
            return false;
        }
        juce::HeapBlock<char> block (kCopyBlockSize);
        for (;;)
        {
            const auto read = in->read (block.get(), kCopyBlockSize);
            if (read <= 0) break;
            if (! out->write (block.get(), (size_t) read))
            {
                error = "Could not write to the chosen destination";
                return false;
            }
        }
        out.reset();
        return true;
    }

    // Copies a local file into a picked folder (an Android document tree).
    // A local folder is not touched here - the caller wrote the files there.
    inline bool writePickedFolderFile (const juce::File& source, const juce::URL& folderUrl,
                                       const juce::String& childName, juce::String& error)
    {
       #if JUCE_ANDROID
        const auto tree = juce::AndroidDocument::fromTree (folderUrl);
        if (! tree.hasValue())
        {
            error = "The chosen folder could not be opened";
            return false;
        }
        // the MIME type carries the extension, so the name is given without it
        const auto nameWithoutExtension = juce::File (childName).getFileNameWithoutExtension();
        const auto child = tree.createChildDocumentWithTypeAndName ("audio/wav", nameWithoutExtension);
        if (! child.hasValue())
        {
            error = "Could not create " + childName;
            return false;
        }
        auto in = source.createInputStream();
        if (in == nullptr)
        {
            error = "Could not read " + source.getFileName();
            return false;
        }
        auto out = child.createOutputStream();
        if (out == nullptr)
        {
            error = "Could not write " + childName;
            return false;
        }
        juce::HeapBlock<char> block (kCopyBlockSize);
        for (;;)
        {
            const auto read = in->read (block.get(), kCopyBlockSize);
            if (read <= 0) break;
            if (! out->write (block.get(), (size_t) read))
            {
                error = "Could not write " + childName;
                return false;
            }
        }
        out.reset();
        return true;
       #else
        (void) source; (void) folderUrl; (void) childName; (void) error;
        return true;   // a local folder: the caller wrote the files there
       #endif
    }
}
