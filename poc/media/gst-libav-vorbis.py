# gst-libav-vorbis.py <GStreamer source> (2026-11-18): Vorbis sound through FFmpeg's decoder.
#
# GStreamer's own Vorbis decoder needs libvorbis, which this build does not have, and gst-libav leaves FFmpeg's
# "vorbis" decoder out on purpose (it counts on the native one). With nothing to decode it, an old WebM (VP8 with
# Vorbis sound) did not play (.ogg files need oggdemux and libogg as well: not built). The changes in gst-libav:
#   - gstavauddec.c: "vorbis" off the list of decoders it skips;
#   - gstavcodecmap.c: Vorbis among the codecs gst-libav knows, with "audio/x-vorbis" as its caps when decoding
#     (without both, avdec_vorbis is not registered at all: the console's first test still said "does not play");
#   - gstavcodecmap.c: the decoder's extradata made from the caps' "streamheader" (matroskademux and oggdemux give
#     the three Vorbis headers there, with no codec_data), Xiph-laced as FFmpeg's avpriv_split_xiph_headers() reads it.
# Run again on a patched tree it changes nothing. It stops if the code around a change is not what it expects.
import sys

root = sys.argv[1].rstrip('/') + '/subprojects/gst-libav/ext/libav/'


def patch(name, old, new):
    path = root + name
    text = open(path, encoding='utf-8').read()
    if new in text:
        print(f'{name}: already patched')
        return
    if text.count(old) != 1:
        sys.exit(f'{name}: the code to change is not there as expected')
    open(path, 'w', encoding='utf-8').write(text.replace(old, new))
    print(f'{name}: patched')


patch('gstavauddec.c',
      '''    if (!strcmp (in_plugin->name, "vorbis") ||
        !strcmp (in_plugin->name, "wavpack") ||''',
      '''    /* BrowserPS5 (2026-11-18): "vorbis" is not skipped: this build has no libvorbis, so FFmpeg's decoder is
     * the only one there is. */
    if (!strcmp (in_plugin->name, "wavpack") ||''')

patch('gstavcodecmap.c',
      '''      context->extradata = NULL;
      context->extradata_size = 0;
      GST_DEBUG ("no codec data");
    }
  }
''',
      '''      context->extradata = NULL;
      context->extradata_size = 0;
      GST_DEBUG ("no codec data");
    }

    /* BrowserPS5 (2026-11-18): Vorbis with no codec_data. matroskademux and oggdemux put the three header
     * packets in "streamheader"; FFmpeg's decoder wants them as its extradata, Xiph-laced: the byte 2, the sizes
     * of the first two (as runs of 255 and the rest), then the three headers one after the other. */
    if (!context->extradata && codec_id == AV_CODEC_ID_VORBIS
        && (value = gst_structure_get_value (str, "streamheader"))
        && GST_VALUE_HOLDS_ARRAY (value) && gst_value_array_get_size (value) == 3) {
      GstMapInfo maps[3];
      gsize total = 1;
      guint i, mapped = 0;

      for (i = 0; i < 3; i++) {
        const GValue *header = gst_value_array_get_value (value, i);
        GstBuffer *hbuf = (header && G_VALUE_HOLDS (header, GST_TYPE_BUFFER))
            ? gst_value_get_buffer (header) : NULL;
        if (!hbuf || !gst_buffer_map (hbuf, &maps[i], GST_MAP_READ))
          break;
        mapped++;
        total += maps[i].size + (i < 2 ? maps[i].size / 255 + 1 : 0);
      }
      if (mapped == 3 && total < G_MAXINT - AV_INPUT_BUFFER_PADDING_SIZE - 16) {
        guint8 *data = av_mallocz (GST_ROUND_UP_16 (total + AV_INPUT_BUFFER_PADDING_SIZE));
        guint8 *at = data;

        if (data) {
          *at++ = 2;
          for (i = 0; i < 2; i++) {
            gsize left = maps[i].size;
            for (; left >= 255; left -= 255)
              *at++ = 255;
            *at++ = (guint8) left;
          }
          for (i = 0; i < 3; i++) {
            memcpy (at, maps[i].data, maps[i].size);
            at += maps[i].size;
          }
          context->extradata = data;
          context->extradata_size = at - data;
          GST_DEBUG ("Vorbis extradata from the stream headers: %d bytes",
              context->extradata_size);
        }
      }
      for (i = 0; i < 3 && i < mapped; i++) {
        const GValue *header = gst_value_array_get_value (value, i);
        gst_buffer_unmap (gst_value_get_buffer (header), &maps[i]);
      }
    }
  }
''')

patch('gstavcodecmap.c',
      '''    case AV_CODEC_ID_FLAC:
    case AV_CODEC_ID_OPUS:
    case AV_CODEC_ID_S302M:
''',
      '''    case AV_CODEC_ID_FLAC:
    case AV_CODEC_ID_OPUS:
    /* BrowserPS5 (2026-11-18): Vorbis too, decoded by FFmpeg (gst-libav-vorbis.py). */
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_S302M:
''')

patch('gstavcodecmap.c',
      '''    case AV_CODEC_ID_S302M:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-smpte-302m", NULL);
      break;
''',
      '''    case AV_CODEC_ID_VORBIS:
      /* BrowserPS5 (2026-11-18): decoding only, like FLAC above. The three header packets come in the caps'
       * "streamheader" and become the decoder's extradata (gst_ffmpeg_caps_with_codecid). */
      if (!encode) {
        caps = gst_caps_new_empty_simple ("audio/x-vorbis");
      }
      break;

    case AV_CODEC_ID_S302M:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-smpte-302m", NULL);
      break;
''')
