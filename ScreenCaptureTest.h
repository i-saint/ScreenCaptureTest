#pragma once

bool ReadTexture(ID3D11Texture2D* tex, int width, int height, const std::function<void(void*, int)>& callback);

// assume data is BGRAUint8
bool SaveAsPNG(const char* path, int w, int h, int stride, const void* data, bool flip_y = false);


void TestGDI();
void TestDesktopDuplication();
void TestGraphicsCapture();

class ProfileTimer
{
public:
    ProfileTimer(const char* mes, ...);
    ~ProfileTimer();

private:
    uint64_t m_begin = 0;
    std::string m_message;
};
#define sctProfile(...) ProfileTimer _dbg_timer(__VA_ARGS__)
