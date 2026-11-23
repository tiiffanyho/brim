#include "preview/preview.hpp"

#include "mini_test.hpp"

using namespace brim;

static capture::Frame frame(int w, int h) {
  capture::Frame f;
  f.width = w;
  f.height = h;
  f.rgb.assign(static_cast<std::size_t>(w) * h * 3, 0);
  return f;
}

TEST_CASE("preview BMP is 320x240, 24-bit, the size camview served") {
  std::vector<std::uint8_t> out;
  preview::encode_bmp(frame(640, 480), out);
  CHECK_EQ(out.size(), static_cast<std::size_t>(54 + 320 * 240 * 3));
  CHECK_EQ(out[0], 'B');
  CHECK_EQ(out[1], 'M');
  CHECK_EQ(out[28], 24);
}

TEST_CASE("preview keeps colours (RGB in, BGR out) and puts the top row last (bottom-up)") {
  auto f = frame(640, 480);
  f.rgb[0] = 200;  // top-left pixel: R
  f.rgb[1] = 100;  // G
  f.rgb[2] = 50;   // B
  std::vector<std::uint8_t> out;
  preview::encode_bmp(f, out);
  const std::size_t top_left = 54 + static_cast<std::size_t>(239) * 320 * 3;
  CHECK_EQ(out[top_left + 0], 50);
  CHECK_EQ(out[top_left + 1], 100);
  CHECK_EQ(out[top_left + 2], 200);
}

TEST_CASE("an empty frame gives a black picture, not a crash") {
  std::vector<std::uint8_t> out;
  preview::encode_bmp(capture::Frame{}, out);
  CHECK_EQ(out.size(), static_cast<std::size_t>(54 + 320 * 240 * 3));
}

MINI_TEST_MAIN()
