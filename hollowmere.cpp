// HOLLOWMERE - a found-footage survival horror. Win32 + Direct3D 12, HLSL SM5, no dependencies.
// Build (VS developer prompt):  see build.bat
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <shellapi.h>
#include <wincodec.h>
#include "hm_game.h"
using Microsoft::WRL::ComPtr;

// ------------------------------------------------------------ D3D objects
static HWND hwnd;
static ComPtr<ID3D12Device> dev;
static ComPtr<ID3D12CommandQueue> queue;
static ComPtr<IDXGISwapChain3> swap;
static ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
static ComPtr<ID3D12CommandAllocator> alloc;
static ComPtr<ID3D12GraphicsCommandList> cl;
static ComPtr<ID3D12Fence> fence;
static HANDLE fenceEvent; static UINT64 fenceValue;
static ComPtr<ID3D12RootSignature> rootSig;
static ComPtr<ID3D12Resource> backBuf[2], sceneTex, sceneDepth, shadowTex, fontRes, vbStatic, vbDyn, vbHud, cbRes, readback;
static ComPtr<ID3D12PipelineState> psoSky, psoScene, psoShadow, psoPost, psoHud;
static UINT rtvStep, dsvStep, srvStep;
static int swapW, swapH;
static FrameCB* cbPtr; static Vtx* dynPtr; static Vtx* hudPtr;
static const int MAX_DYN = 200000, MAX_HUD = 60000, SHADOW_RES = 4096;
static const DXGI_FORMAT HDR_FMT = DXGI_FORMAT_R16G16B16A16_FLOAT;
static UINT chunkStart[NCH], chunkCount[NCH];
static Mesh dynMesh;
static bool fullscreen = false; static RECT windowedRect; static bool cursorHidden = false, mouseArmed = false;

static bool fail(const char* m) { MessageBoxA(nullptr, m, "HOLLOWMERE", MB_ICONERROR); return false; }

static void waitGpu() {
    queue->Signal(fence.Get(), ++fenceValue);
    if (fence->GetCompletedValue() < fenceValue) { fence->SetEventOnCompletion(fenceValue, fenceEvent); WaitForSingleObject(fenceEvent, INFINITE); }
}
static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x = {};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource = r; x.Transition.StateBefore = a; x.Transition.StateAfter = b;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; return x;
}
static D3D12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE t) { D3D12_HEAP_PROPERTIES h = {}; h.Type = t; return h; }
static D3D12_RESOURCE_DESC bufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC d = {}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; return d;
}
static bool makeBuffer(UINT64 bytes, ComPtr<ID3D12Resource>& out, void** mapped, D3D12_HEAP_TYPE type = D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATES st = D3D12_RESOURCE_STATE_GENERIC_READ) {
    D3D12_HEAP_PROPERTIES hp = heapProps(type); D3D12_RESOURCE_DESC d = bufferDesc(bytes);
    if (type == D3D12_HEAP_TYPE_READBACK) st = D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, nullptr, IID_PPV_ARGS(&out)))) return false;
    if (mapped) { D3D12_RANGE none = { 0, 0 }; return SUCCEEDED(out->Map(0, &none, mapped)); }
    return true;
}
static bool makeTex(int w, int h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES st, const D3D12_CLEAR_VALUE* cv, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC d = {}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = fmt; d.SampleDesc.Count = 1; d.Flags = flags;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, cv, IID_PPV_ARGS(&out)));
}

static D3D12_CPU_DESCRIPTOR_HANDLE cpuAt(ID3D12DescriptorHeap* h, UINT step, int i) { auto c = h->GetCPUDescriptorHandleForHeapStart(); c.ptr += (SIZE_T)step * i; return c; }

static bool makeTargets(int w, int h) {
    for (int i = 0; i < 2; i++) {
        if (FAILED(swap->GetBuffer(i, IID_PPV_ARGS(&backBuf[i])))) return false;
        dev->CreateRenderTargetView(backBuf[i].Get(), nullptr, cpuAt(rtvHeap.Get(), rtvStep, i));
    }
    D3D12_CLEAR_VALUE cc = {}; cc.Format = HDR_FMT; cc.Color[0] = 0.004f; cc.Color[1] = 0.007f; cc.Color[2] = 0.012f; cc.Color[3] = 1;
    if (!makeTex(w, h, HDR_FMT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cc, sceneTex)) return false;
    dev->CreateRenderTargetView(sceneTex.Get(), nullptr, cpuAt(rtvHeap.Get(), rtvStep, 2));
    D3D12_CLEAR_VALUE dc = {}; dc.Format = DXGI_FORMAT_D32_FLOAT; dc.DepthStencil.Depth = 1;
    if (!makeTex(w, h, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &dc, sceneDepth)) return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(sceneDepth.Get(), &dv, cpuAt(dsvHeap.Get(), dsvStep, 0));
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    sv.Format = HDR_FMT; dev->CreateShaderResourceView(sceneTex.Get(), &sv, cpuAt(srvHeap.Get(), srvStep, 1));
    sv.Format = DXGI_FORMAT_R32_FLOAT; dev->CreateShaderResourceView(sceneDepth.Get(), &sv, cpuAt(srvHeap.Get(), srvStep, 3));
    swapW = w; swapH = h;
    return true;
}

static bool compile(const std::string& src, const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompile(src.data(), src.size(), "hollowmere.hlsl", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
    if (FAILED(hr)) {
        std::string m = std::string("Shader ") + entry + " failed:\n" + (err ? (const char*)err->GetBufferPointer() : "unknown error");
        FILE* f = fopen("shader_error.txt", "wb"); if (f) { fwrite(m.data(), 1, m.size(), f); fclose(f); }
        return fail(m.c_str());
    }
    return true;
}

static const D3D12_INPUT_ELEMENT_DESC LAYOUT[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};
static bool makePso(ComPtr<ID3D12PipelineState>& out, ID3DBlob* vs, ID3DBlob* ps, bool layout, bool depth, bool blend, DXGI_FORMAT rt, int nrt) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = rootSig.Get();
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    if (ps) d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; d.RasterizerState.DepthClipEnable = TRUE;
    auto& r = d.BlendState.RenderTarget[0];
    r.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (blend) {
        r.BlendEnable = TRUE; r.SrcBlend = D3D12_BLEND_SRC_ALPHA; r.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; r.BlendOp = D3D12_BLEND_OP_ADD;
        r.SrcBlendAlpha = D3D12_BLEND_ONE; r.DestBlendAlpha = D3D12_BLEND_ONE; r.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    d.DepthStencilState.DepthEnable = depth;
    d.DepthStencilState.DepthWriteMask = depth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    if (layout) d.InputLayout = { LAYOUT, 4 };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = nrt; if (nrt) d.RTVFormats[0] = rt;
    d.DSVFormat = (depth || rt == HDR_FMT) ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    return SUCCEEDED(dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&out)));
}

// ------------------------------------------------------------ font atlas (GDI) and HUD text
struct Glyph { float u0, v0, u1, v1; int adv; };
static Glyph glyphs[3][95]; static int fontCellW[3], fontCellH[3], fontAscent[3];
static const int AW = 1024, AH = 1024;
static float whiteU, whiteV;

static bool makeFontAtlas() {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = AW; bi.bmiHeader.biHeight = -AH; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return false;
    SelectObject(mem, bmp); memset(bits, 0, (size_t)AW * AH * 4);
    SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(255, 255, 255));
    struct F { int size, weight; const wchar_t* name; int cw, ch; } fonts[3] = { { 22, FW_NORMAL, L"Consolas", 16, 30 }, { 66, FW_BOLD, L"Georgia", 58, 82 }, { 17, FW_NORMAL, L"Consolas", 12, 22 } };
    int y0 = 0;
    for (int f = 0; f < 3; f++) {
        HFONT font = CreateFontW(fonts[f].size, 0, 0, 0, fonts[f].weight, 0, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, fonts[f].name);
        SelectObject(mem, font);
        TEXTMETRICW tm; GetTextMetricsW(mem, &tm);
        fontAscent[f] = tm.tmAscent; fontCellW[f] = fonts[f].cw; fontCellH[f] = fonts[f].ch;
        int perRow = AW / fonts[f].cw;
        for (int ch = 32; ch < 127; ch++) {
            int idx = ch - 32, cx = (idx % perRow) * fonts[f].cw, cy = y0 + (idx / perRow) * fonts[f].ch;
            char c = (char)ch; SIZE sz;
            TextOutA(mem, cx, cy, &c, 1);
            GetTextExtentPoint32A(mem, &c, 1, &sz);
            glyphs[f][idx] = { cx / (float)AW, cy / (float)AH, (cx + fonts[f].cw) / (float)AW, (cy + fonts[f].ch) / (float)AH, sz.cx };
        }
        y0 += ((95 + perRow - 1) / perRow) * fonts[f].ch + 4;
        SelectObject(mem, GetStockObject(SYSTEM_FONT)); DeleteObject(font);
    }
    GdiFlush();
    uint32_t* px = (uint32_t*)bits;
    for (int y = AH - 8; y < AH; y++) for (int x = AW - 8; x < AW; x++) px[(size_t)y * AW + x] = 0xFFFFFFFF;
    whiteU = (AW - 4) / (float)AW; whiteV = (AH - 4) / (float)AH;

    D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = AW; td.Height = AH; td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&fontRes)))) return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 total = 0;
    dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
    ComPtr<ID3D12Resource> up; void* upPtr = nullptr;
    if (!makeBuffer(total, up, &upPtr)) return false;
    for (int y = 0; y < AH; y++) memcpy((char*)upPtr + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (char*)bits + (size_t)y * AW * 4, AW * 4);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = fontRes.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    alloc->Reset(); cl->Reset(alloc.Get(), nullptr);
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b = transition(fontRes.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &b); cl->Close();
    ID3D12CommandList* lists[] = { cl.Get() }; queue->ExecuteCommandLists(1, lists); waitGpu();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(fontRes.Get(), &sv, cpuAt(srvHeap.Get(), srvStep, 0));
    DeleteObject(bmp); DeleteDC(mem);
    return true;
}

static int nHud = 0;
static void hudVert(float x, float y, Col c, float a, float u, float v) {
    if (nHud >= MAX_HUD) return;
    Vtx& o = hudPtr[nHud++];
    o.p[0] = x; o.p[1] = y; o.p[2] = 0; o.n[0] = o.n[1] = o.n[2] = o.n[3] = 0;
    o.c[0] = pu(c.r); o.c[1] = pu(c.g); o.c[2] = pu(c.b); o.c[3] = pu(a); o.uv[0] = u; o.uv[1] = v;
}
static void quad2(float l, float t, float r, float b, Col c, float a = 1) {
    hudVert(l, t, c, a, whiteU, whiteV); hudVert(r, t, c, a, whiteU, whiteV); hudVert(r, b, c, a, whiteU, whiteV);
    hudVert(l, t, c, a, whiteU, whiteV); hudVert(r, b, c, a, whiteU, whiteV); hudVert(l, b, c, a, whiteU, whiteV);
}
static void disc(float cx, float cy, float rad, Col c, float a) {
    for (int i = 0; i < 16; i++) {
        float a0 = i * 2 * PI / 16, a1 = (i + 1) * 2 * PI / 16;
        hudVert(cx, cy, c, a, whiteU, whiteV); hudVert(cx + cosf(a0) * rad, cy + sinf(a0) * rad, c, a, whiteU, whiteV); hudVert(cx + cosf(a1) * rad, cy + sinf(a1) * rad, c, a, whiteU, whiteV);
    }
}
static int textWidth(const char* s, int f) { int w = 0; for (; *s; s++) if (*s >= 32 && *s < 127) w += glyphs[f][*s - 32].adv; return w; }
static void textRaw(float x, float y, const char* s, int f, Col c, float a) {
    for (; *s; s++) {
        if (*s < 32 || *s >= 127) continue;
        Glyph& g2 = glyphs[f][*s - 32];
        float w = (float)fontCellW[f], h = (float)fontCellH[f];
        hudVert(x, y, c, a, g2.u0, g2.v0); hudVert(x + w, y, c, a, g2.u1, g2.v0); hudVert(x + w, y + h, c, a, g2.u1, g2.v1);
        hudVert(x, y, c, a, g2.u0, g2.v0); hudVert(x + w, y + h, c, a, g2.u1, g2.v1); hudVert(x, y + h, c, a, g2.u0, g2.v1);
        x += (float)g2.adv;
    }
}
// align: 0 left, 1 centre, 2 right
static void text(float x, float y, const char* s, int f, int align, Col c, float a = 1, bool shadow = true) {
    float w = (float)textWidth(s, f);
    if (align == 1) x -= w * 0.5f; else if (align == 2) x -= w;
    if (shadow) textRaw(x + 1.5f, y + 1.5f, s, f, { 0, 0, 0 }, a * 0.8f);
    textRaw(x, y, s, f, c, a);
}
static std::vector<std::string> wrap(const std::string& s, int f, float maxw) {
    std::vector<std::string> lines; std::string cur, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string t = cur.empty() ? word : cur + " " + word;
        if (textWidth(t.c_str(), f) > maxw && !cur.empty()) { lines.push_back(cur); cur = word; } else cur = t;
        word.clear();
    };
    for (char ch : s) { if (ch == ' ') flush(); else word += ch; }
    flush(); if (!cur.empty()) lines.push_back(cur);
    return lines;
}

static void buildHud(int w, int h) {
    nHud = 0;
    float W = (float)w, H = (float)h, s = H / 720.0f;
    Col white = { 0.92f, 0.94f, 0.9f }, red = { 0.95f, 0.1f, 0.08f };
    float m = 34 * s;
    auto drawCentered = [&](const char* t, float y, int f, Col c, float a = 1) { text(W * 0.5f, y, t, f, 1, c, a); };

    if (g.state == ST_MENU) {
        quad2(0, 0, W, H, { 0, 0, 0 }, 0.55f);
        drawCentered("HOLLOWMERE", H * 0.24f, 1, { 0.85f, 0.82f, 0.74f });
        drawCentered("RECOVERED FOOTAGE  -  TAPE 03", H * 0.24f + 92 * s, 0, { 0.7f, 0.7f, 0.65f }, 0.8f);
        float y = H * 0.55f;
        const char* lines[] = { "You and your friends went camping. Only the camera is left.", "", "WASD  walk        SHIFT  run        C  crouch        MOUSE  look",
                                "F  flashlight        N  night vision        E  interact / hold", "ESC  pause", "", "It only moves when you are not looking at it. Keep the light on it." };
        for (const char* l : lines) { text(W * 0.5f, y, l, 0, 1, { 0.8f, 0.82f, 0.78f }, 0.9f); y += 30 * s; }
        if (fmodf(g.time, 1.2f) < 0.8f) drawCentered("PRESS ENTER", H * 0.88f, 0, { 1, 1, 1 });
        return;
    }
    float aHud = (g.state == ST_PLAY || g.state == ST_PAUSE) ? 1.0f : 0.5f;
    // REC indicator and elapsed time
    bool blink = fmodf(g.time, 1.2f) < 0.7f;
    if (blink) disc(m + 8 * s, m + 14 * s, 8 * s, red, aHud);
    int sec = (int)g.playT; char buf[128];
    snprintf(buf, sizeof buf, "REC  %02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    text(m + 24 * s, m, buf, 0, 0, white, aHud);
    // clock and date
    int clk = 23 * 3600 + 47 * 60 + sec; int hh = (clk / 3600) % 24, mm = (clk / 60) % 60, ss = clk % 60;
    int h12 = hh % 12 == 0 ? 12 : hh % 12;
    snprintf(buf, sizeof buf, "OCT %d 1994  %02d:%02d:%02d %s", clk >= 86400 ? 30 : 29, h12, mm, ss, hh < 12 ? "AM" : "PM");
    text(m, H - m - 28 * s, buf, 0, 0, white, 0.9f * aHud);
    // battery
    float bx = W - m - 110 * s, by = m + 3 * s;
    float bat = g.battery / 100.0f;
    Col bc = bat < 0.2f ? red : white;
    quad2(bx, by, bx + 90 * s, by + 4 * s, bc, 0.9f * aHud); quad2(bx, by + 18 * s, bx + 90 * s, by + 22 * s, bc, 0.9f * aHud);
    quad2(bx, by, bx + 4 * s, by + 22 * s, bc, 0.9f * aHud); quad2(bx + 86 * s, by, bx + 90 * s, by + 22 * s, bc, 0.9f * aHud); quad2(bx + 90 * s, by + 6 * s, bx + 95 * s, by + 16 * s, bc, 0.9f * aHud);
    quad2(bx + 7 * s, by + 7 * s, bx + 7 * s + 76 * s * bat, by + 15 * s, bc, 0.85f * aHud);
    if (g.nv) text(W - m, m + 30 * s, "NIGHT VISION", 2, 2, { 0.4f, 1, 0.5f }, aHud);
    else text(W - m, m + 30 * s, g.flash ? "LIGHT ON" : "LIGHT OFF", 2, 2, white, 0.6f * aHud);
    if (g.stamina < 0.98f) { quad2(W - m - 90 * s, m + 52 * s, W - m, m + 55 * s, white, 0.15f); quad2(W - m - 90 * s, m + 52 * s, W - m - 90 * s + 90 * s * g.stamina, m + 55 * s, white, 0.6f * aHud); }
    // objective
    if (g.state == ST_PLAY && !g.objective.empty()) text(W * 0.5f, m, g.objective.c_str(), 2, 1, { 0.85f, 0.8f, 0.6f }, 0.75f);
    // crosshair
    if (g.state == ST_PLAY) disc(W * 0.5f, H * 0.5f, 1.8f * s, white, 0.35f);
    // interaction prompt + hold progress
    if (g.state == ST_PLAY && !g.prompt.empty()) {
        text(W * 0.5f, H * 0.57f, g.prompt.c_str(), 0, 1, white, 0.95f);
        if (g.holdT > 0.01f) { float p = clampf(g.holdT / 1.7f, 0, 1); quad2(W * 0.5f - 70 * s, H * 0.57f + 36 * s, W * 0.5f + 70 * s, H * 0.57f + 40 * s, white, 0.2f); quad2(W * 0.5f - 70 * s, H * 0.57f + 36 * s, W * 0.5f - 70 * s + 140 * s * p, H * 0.57f + 40 * s, red, 0.9f); }
    }
    // subtitles
    if (!g.subs.empty() && g.state == ST_PLAY) {
        Sub& sb = g.subs[0];
        float a = clampf(std::min(sb.t * 3.0f, (sb.dur - sb.t) * 2.0f), 0, 1);
        int f = sb.note ? 2 : 0;
        auto lines = wrap(sb.text, f, W * (sb.note ? 0.55f : 0.62f));
        float lh = (sb.note ? 26.0f : 34.0f) * s, y = H * (sb.note ? 0.70f : 0.80f) - lh * (float)lines.size() * 0.5f;
        if (sb.note) { float bw = W * 0.6f; quad2(W * 0.5f - bw * 0.5f, y - 14 * s, W * 0.5f + bw * 0.5f, y + lh * lines.size() + 10 * s, { 0.03f, 0.03f, 0.03f }, 0.6f * a); }
        for (auto& l : lines) { text(W * 0.5f, y, l.c_str(), f, 1, sb.note ? Col{ 0.9f, 0.86f, 0.7f } : Col{ 0.95f, 0.95f, 0.92f }, a); y += lh; }
    }
    if (g.state == ST_PAUSE) {
        quad2(0, 0, W, H, { 0, 0, 0 }, 0.6f);
        drawCentered("PAUSED", H * 0.38f, 1, white);
        drawCentered("ESC  resume        Q  quit", H * 0.38f + 100 * s, 0, white);
    }
    if (g.state == ST_DEAD && g.deathT > 1.3f) {
        float a = clampf((g.deathT - 1.3f) * 1.5f, 0, 1);
        drawCentered("THE TAPE ENDS HERE", H * 0.40f, 1, { 0.8f, 0.1f, 0.08f }, a);
        if (g.deathT > 3.2f) drawCentered("PRESS ENTER TO REWIND", H * 0.40f + 110 * s, 0, white, a);
    }
    if (g.state == ST_WIN && g.winT > 1.2f) {
        float a = clampf((g.winT - 1.2f) * 1.0f, 0, 1);
        drawCentered("YOU MADE IT OUT", H * 0.36f, 1, white, a);
        if (g.winT > 3.0f) {
            drawCentered("But the camera is still recording.", H * 0.36f + 100 * s, 0, { 0.8f, 0.8f, 0.75f }, a);
            drawCentered("Review the footage. Someone is standing behind you in every shot.", H * 0.36f + 136 * s, 0, { 0.8f, 0.1f, 0.08f }, a);
            if (g.winT > 5.0f) drawCentered("PRESS ENTER", H * 0.36f + 200 * s, 0, white, a);
        }
    }
}

// ------------------------------------------------------------ photoscanned textures (Poly Haven, CC0) via WIC
static const char* TEX_IDS[10] = { "forest_ground_04", "bark_brown_02", "wood_floor_worn", "weathered_planks", "decrepit_wallpaper", "dark_wood", "mossy_rock", "roof_slates_02", "rusty_metal_02", "dirty_carpet" };
static const int NTEX = 10, TEXSZ = 1024, TEXMIPS = 11;
static ComPtr<ID3D12Resource> texDiffRes, texNrmRes;

static bool loadImage(IWICImagingFactory* f, const std::wstring& path, std::vector<uint8_t>& out) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(dec->GetFrame(0, &fr))) return false;
    ComPtr<IWICFormatConverter> conv; if (FAILED(f->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT w, h; conv->GetSize(&w, &h);
    out.assign((size_t)TEXSZ * TEXSZ * 4, 0);
    if (w == TEXSZ && h == TEXSZ) return SUCCEEDED(conv->CopyPixels(nullptr, TEXSZ * 4, (UINT)out.size(), out.data()));
    ComPtr<IWICBitmapScaler> sc; if (FAILED(f->CreateBitmapScaler(&sc))) return false;
    if (FAILED(sc->Initialize(conv.Get(), TEXSZ, TEXSZ, WICBitmapInterpolationModeFant))) return false;
    return SUCCEEDED(sc->CopyPixels(nullptr, TEXSZ * 4, (UINT)out.size(), out.data()));
}
static void buildMips(std::vector<uint8_t>* mips) {         // mips[0] is filled, box-filter the rest
    for (int m = 1; m < TEXMIPS; m++) {
        int w = TEXSZ >> m, sw = w * 2; mips[m].assign((size_t)w * w * 4, 0);
        for (int y = 0; y < w; y++) for (int x = 0; x < w; x++) for (int c = 0; c < 4; c++) {
            const uint8_t* s = &mips[m - 1][((size_t)(y * 2) * sw + x * 2) * 4 + c];
            mips[m][((size_t)y * w + x) * 4 + c] = (uint8_t)((s[0] + s[4] + s[sw * 4] + s[sw * 4 + 4] + 2) / 4);
        }
    }
}
static bool loadTextures() {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe; dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"textures\\";
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    bool haveWic = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    // per slice, per mip
    std::vector<std::vector<uint8_t>> diff((size_t)NTEX * TEXMIPS), nrm((size_t)NTEX * TEXMIPS);
    int loaded = 0;
    for (int t = 0; t < NTEX; t++) {
        std::vector<uint8_t> d, n, r;
        std::wstring base = dir; for (const char* c = TEX_IDS[t]; *c; c++) base += (wchar_t)*c;
        bool ok = haveWic && loadImage(wic.Get(), base + L"_diff.jpg", d) && loadImage(wic.Get(), base + L"_nor.jpg", n) && loadImage(wic.Get(), base + L"_rough.jpg", r);
        if (!ok) { d.assign((size_t)TEXSZ * TEXSZ * 4, 90); n.assign((size_t)TEXSZ * TEXSZ * 4, 128); r.assign((size_t)TEXSZ * TEXSZ * 4, 200); for (size_t i = 3; i < d.size(); i += 4) d[i] = n[i] = r[i] = 255; }
        else loaded++;
        for (size_t i = 0; i < (size_t)TEXSZ * TEXSZ; i++) { n[i * 4 + 2] = r[i * 4]; n[i * 4 + 3] = 255; }   // pack roughness into the normal map's blue
        diff[(size_t)t * TEXMIPS] = std::move(d); nrm[(size_t)t * TEXMIPS] = std::move(n);
        buildMips(&diff[(size_t)t * TEXMIPS]); buildMips(&nrm[(size_t)t * TEXMIPS]);
    }
    (void)loaded;
    D3D12_HEAP_PROPERTIES hp = heapProps(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = TEXSZ; td.Height = TEXSZ; td.DepthOrArraySize = NTEX; td.MipLevels = TEXMIPS; td.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> up[2]; ID3D12Resource* dst[2];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2][NTEX * TEXMIPS]; UINT rows[2][NTEX * TEXMIPS]; UINT64 rowSz[2][NTEX * TEXMIPS];
    DXGI_FORMAT fmts[2] = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM };
    ComPtr<ID3D12Resource>* outs[2] = { &texDiffRes, &texNrmRes };
    std::vector<std::vector<uint8_t>>* srcs[2] = { &diff, &nrm };
    for (int k = 0; k < 2; k++) {
        td.Format = fmts[k];
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(outs[k]->GetAddressOf())))) return false;
        dst[k] = outs[k]->Get();
        UINT64 total = 0;
        dev->GetCopyableFootprints(&td, 0, NTEX * TEXMIPS, 0, fp[k], rows[k], rowSz[k], &total);
        void* mp = nullptr;
        if (!makeBuffer(total, up[k], &mp)) return false;
        for (int s = 0; s < NTEX; s++) for (int m = 0; m < TEXMIPS; m++) {
            int sub = m + s * TEXMIPS, w = TEXSZ >> m; const std::vector<uint8_t>& src = (*srcs[k])[(size_t)s * TEXMIPS + m];
            for (int y = 0; y < w; y++) memcpy((char*)mp + fp[k][sub].Offset + (size_t)y * fp[k][sub].Footprint.RowPitch, &src[(size_t)y * w * 4], (size_t)w * 4);
        }
    }
    alloc->Reset(); cl->Reset(alloc.Get(), nullptr);
    for (int k = 0; k < 2; k++) for (int sub = 0; sub < NTEX * TEXMIPS; sub++) {
        D3D12_TEXTURE_COPY_LOCATION d2 = {}, s2 = {};
        d2.pResource = dst[k]; d2.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d2.SubresourceIndex = sub;
        s2.pResource = up[k].Get(); s2.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s2.PlacedFootprint = fp[k][sub];
        cl->CopyTextureRegion(&d2, 0, 0, 0, &s2, nullptr);
    }
    D3D12_RESOURCE_BARRIER b[2] = { transition(dst[0], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE), transition(dst[1], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) };
    cl->ResourceBarrier(2, b); cl->Close();
    ID3D12CommandList* lists[] = { cl.Get() }; queue->ExecuteCommandLists(1, lists); waitGpu();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2DArray.MipLevels = TEXMIPS; sv.Texture2DArray.ArraySize = NTEX;
    sv.Format = fmts[0]; dev->CreateShaderResourceView(dst[0], &sv, cpuAt(srvHeap.Get(), srvStep, 4));
    sv.Format = fmts[1]; dev->CreateShaderResourceView(dst[1], &sv, cpuAt(srvHeap.Get(), srvStep, 5));
    return true;
}

// ------------------------------------------------------------ init
static bool initD3D(int w, int h) {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return fail("Could not create the DXGI factory.");
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) || FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) return fail("No Direct3D 12 device found.");
    }
    D3D12_COMMAND_QUEUE_DESC qd = {};
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return fail("Could not create the command queue.");
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w; sd.Height = h; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sd, nullptr, nullptr, &sc1)) || FAILED(sc1.As(&swap))) return fail("Could not create the swap chain.");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 3; dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 2; dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 6; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap));
    if (!rtvHeap || !dsvHeap || !srvHeap) return fail("Could not create descriptor heaps.");
    rtvStep = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvStep = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    srvStep = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
        FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl))) ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return fail("Could not create the command list.");
    cl->Close();
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!makeTargets(w, h)) return fail("Could not create the render targets.");

    // shadow map
    D3D12_CLEAR_VALUE dc = {}; dc.Format = DXGI_FORMAT_D32_FLOAT; dc.DepthStencil.Depth = 1;
    if (!makeTex(SHADOW_RES, SHADOW_RES, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &dc, shadowTex)) return fail("Could not create the shadow map.");
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(shadowTex.Get(), &dv, cpuAt(dsvHeap.Get(), dsvStep, 1));
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(shadowTex.Get(), &sv, cpuAt(srvHeap.Get(), srvStep, 2));

    // root signature
    D3D12_DESCRIPTOR_RANGE range = {}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 6;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_STATIC_SAMPLER_DESC samp[3] = {};
    samp[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; samp[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; samp[2].Filter = D3D12_FILTER_ANISOTROPIC;
    for (int i = 0; i < 3; i++) { samp[i].AddressU = samp[i].AddressV = samp[i].AddressW = i == 2 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP; samp[i].MaxLOD = D3D12_FLOAT32_MAX; samp[i].MaxAnisotropy = 8; samp[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; samp[i].ShaderRegister = i; samp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; }
    D3D12_ROOT_SIGNATURE_DESC rs = {}; rs.NumParameters = 2; rs.pParameters = params; rs.NumStaticSamplers = 3; rs.pStaticSamplers = samp;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) || FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) return fail("Could not create the root signature.");

    std::string src = std::string(HLSL_COMMON) + HLSL_SCENE + HLSL_POST;
    ComPtr<ID3DBlob> vsScene, psScene, vsShadow, vsFull, psSky, psPost, vsHud, psHud;
    if (!compile(src, "VS_scene", "vs_5_0", vsScene) || !compile(src, "PS_scene", "ps_5_0", psScene) || !compile(src, "VS_shadow", "vs_5_0", vsShadow) ||
        !compile(src, "VS_full", "vs_5_0", vsFull) || !compile(src, "PS_sky", "ps_5_0", psSky) || !compile(src, "PS_post", "ps_5_0", psPost) ||
        !compile(src, "VS_hud", "vs_5_0", vsHud) || !compile(src, "PS_hud", "ps_5_0", psHud)) return false;
    if (!makePso(psoSky, vsFull.Get(), psSky.Get(), false, false, false, HDR_FMT, 1) ||
        !makePso(psoScene, vsScene.Get(), psScene.Get(), true, true, false, HDR_FMT, 1) ||
        !makePso(psoShadow, vsShadow.Get(), nullptr, true, true, false, DXGI_FORMAT_UNKNOWN, 0) ||
        !makePso(psoPost, vsFull.Get(), psPost.Get(), false, false, false, DXGI_FORMAT_R8G8B8A8_UNORM, 1) ||
        !makePso(psoHud, vsHud.Get(), psHud.Get(), true, false, true, DXGI_FORMAT_R8G8B8A8_UNORM, 1)) return fail("Could not create the pipeline states.");

    void* p = nullptr;
    if (!makeBuffer((UINT64)MAX_DYN * sizeof(Vtx), vbDyn, &p)) return fail("Could not allocate the dynamic vertex buffer."); dynPtr = (Vtx*)p;
    if (!makeBuffer((UINT64)MAX_HUD * sizeof(Vtx), vbHud, &p)) return fail("Could not allocate the HUD buffer."); hudPtr = (Vtx*)p;
    if (!makeBuffer(1024, cbRes, &p)) return fail("Could not allocate the constant buffer."); cbPtr = (FrameCB*)p;
    if (!makeFontAtlas()) return fail("Could not build the font texture.");
    if (!loadTextures()) return fail("Could not create the material textures.");
    return true;
}

static bool uploadStatic() {
    size_t total = 0;
    for (int i = 0; i < NCH; i++) total += chMesh[i].v.size();
    std::vector<Vtx> all; all.reserve(total);
    for (int i = 0; i < NCH; i++) { chunkStart[i] = (UINT)all.size(); chunkCount[i] = (UINT)chMesh[i].v.size(); all.insert(all.end(), chMesh[i].v.begin(), chMesh[i].v.end()); chMesh[i].v.clear(); chMesh[i].v.shrink_to_fit(); }
    UINT64 bytes = (UINT64)all.size() * sizeof(Vtx);
    ComPtr<ID3D12Resource> up; void* upPtr = nullptr;
    if (!makeBuffer(bytes, up, &upPtr)) return fail("Could not allocate the static upload buffer.");
    memcpy(upPtr, all.data(), bytes);
    if (!makeBuffer(bytes, vbStatic, nullptr, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST)) return fail("Could not allocate the static vertex buffer.");
    alloc->Reset(); cl->Reset(alloc.Get(), nullptr);
    cl->CopyBufferRegion(vbStatic.Get(), 0, up.Get(), 0, bytes);
    D3D12_RESOURCE_BARRIER b = transition(vbStatic.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cl->ResourceBarrier(1, &b); cl->Close();
    ID3D12CommandList* lists[] = { cl.Get() }; queue->ExecuteCommandLists(1, lists); waitGpu();
    return true;
}

static void resizeIfNeeded(int w, int h) {
    if (w == swapW && h == swapH) return;
    waitGpu();
    backBuf[0].Reset(); backBuf[1].Reset(); sceneTex.Reset(); sceneDepth.Reset();
    swap->ResizeBuffers(2, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    makeTargets(w, h);
}

// ------------------------------------------------------------ per-frame constants
static void setViewMatrix(float* m, V3 pos, V3 F, V3 R, V3 U, float tx, float ty, float zn, float zf) {
    float A = zf / (zf - zn), B = -zn * zf / (zf - zn), xs = 1 / tx, ys = 1 / ty;
    m[0] = R.x * xs; m[1] = U.x * ys; m[2] = F.x * A; m[3] = F.x;
    m[4] = R.y * xs; m[5] = U.y * ys; m[6] = F.y * A; m[7] = F.y;
    m[8] = R.z * xs; m[9] = U.z * ys; m[10] = F.z * A; m[11] = F.z;
    m[12] = -dot(R, pos) * xs; m[13] = -dot(U, pos) * ys; m[14] = -dot(F, pos) * A + B; m[15] = -dot(F, pos);
}
static const float VIEW_ZN = 0.05f, VIEW_ZF = 300.0f, LIGHT_ZN = 0.2f, LIGHT_ZF = 60.0f, FLASH_TAN = 0.62f;
static float g_flashIntensity = 0;

static void fillCB(int w, int h) {
    FrameCB cb = {};
    float tanY = tanf(34.0f * PI / 180.0f), asp = (float)w / std::max(1, h), tanX = tanY * asp;
    setViewMatrix(cb.viewProj, g.eye, g.camF, g.camR, g.camU, tanX, tanY, VIEW_ZN, VIEW_ZF);
    V3 F = g.flashD, R = { -F.z, 0, F.x };
    if (len(R) < 0.05f) R = { 1, 0, 0 }; R = norm(R);
    V3 U = cross(R, F);
    setViewMatrix(cb.lightVP, g.flashP, F, R, U, FLASH_TAN, FLASH_TAN, LIGHT_ZN, LIGHT_ZF);
    auto set4 = [](float* d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; };
    set4(cb.camPos, g.eye.x, g.eye.y, g.eye.z, g.time);
    set4(cb.camFwd, g.camF.x, g.camF.y, g.camF.z, VIEW_ZN);
    set4(cb.camRight, g.camR.x, g.camR.y, g.camR.z, tanX);
    set4(cb.camUp, g.camU.x, g.camU.y, g.camU.z, tanY);
    float fi = (g.flash && g.battery > 0) ? 1.0f : 0.0f;
    if (g.battery < 18 && g.flash) fi *= 0.55f + 0.45f * (vnoise(g.time * 14.0f, 3.0f) > 0.35f ? 1.0f : 0.0f);
    if (g.battery < 5 && g.flash) fi *= 0.5f;
    if (g.fear > 0.55f && g.flash) fi *= 1.0f - 0.8f * (vnoise(g.time * 22.0f, 9.0f) > 1.05f - g.fear * 0.4f ? 1.0f : 0.0f);
    g_flashIntensity = fi;
    set4(cb.flashPos, g.flashP.x, g.flashP.y, g.flashP.z, fi);
    set4(cb.flashDir, F.x, F.y, F.z, 0.9205f);
    bool nvOn = g.nv && g.battery > 0;
    set4(cb.params, g.fogNow, g.indoors, 0.45f, nvOn ? 0.05f : 0.022f);
    set4(cb.fogCol, 0.030f, 0.040f, 0.056f, VIEW_ZF);
    float glitch = clampf(g.fear * 0.75f + g.staticT * 0.5f, 0, 1);
    if (g.state == ST_DEAD) glitch = 1;
    set4(cb.post0, nvOn ? 1.0f : 0.0f, glitch, 0.72f + 0.28f * g.fear, 0.085f + g.fear * 0.05f + (nvOn ? 0.06f : 0));
    float bf = g.battery < 12 ? (1 - g.battery / 12.0f) : 0;
    set4(cb.post1, (g.state == ST_MENU) ? 1.0f : clampf(g.fade, 0, 1), bf, g.glow, 1.9f);
    set4(cb.screen, (float)w, (float)h, 1.0f / w, 1.0f / h);
    set4(cb.lzn, LIGHT_ZN, LIGHT_ZF, 1.0f / SHADOW_RES, 0);
    selectLamps(cb);
    *cbPtr = cb;
}

// ------------------------------------------------------------ frame
static bool wantShot = false; static std::string shotPath;

static void writeBmp(const char* path, const uint8_t* rgba, int w, int h, int pitch) {
    FILE* f = fopen(path, "wb"); if (!f) return;
    int rowBytes = (w * 3 + 3) & ~3;
    BITMAPFILEHEADER fh = {}; BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + rowBytes * h;
    ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1; ih.biBitCount = 24; ih.biCompression = BI_RGB;
    fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* s = rgba + (size_t)y * pitch;
        for (int x = 0; x < w; x++) { row[x * 3] = s[x * 4 + 2]; row[x * 3 + 1] = s[x * 4 + 1]; row[x * 3 + 2] = s[x * 4]; }
        fwrite(row.data(), 1, rowBytes, f);
    }
    fclose(f);
}

static void drawChunks(bool shadow, D3D12_VERTEX_BUFFER_VIEW& view) {
    cl->IASetVertexBuffers(0, 1, &view);
    for (int c = 0; c < NCH; c++) {
        if (!chunkCount[c]) continue;
        V3 ctr = { 0, 0, 0 }; float rad = 20;
        if (c != MCH) { int cx = c % NC, cz = c / NC; ctr = { -WHALF + (cx + 0.5f) * CELL, 0, -WHALF + (cz + 0.5f) * CELL }; rad = 17; }
        else rad = 18;
        V3 from = shadow ? g.flashP : g.eye, fw = shadow ? g.flashD : g.camF;
        V3 d = ctr - from; d.y = 0; float dist = len(d);
        float maxD = shadow ? 52.0f : 80.0f;
        if (dist > maxD + rad) continue;
        if (dist > rad && dot(d * (1 / dist), V3{ fw.x, 0, fw.z }) < -0.35f - rad / dist * 0.5f && dist > rad * 1.5f) { if (!shadow || dist > 24) continue; }
        cl->DrawInstanced(chunkCount[c], 1, chunkStart[c], 0);
    }
}

static void renderFrame(int w, int h) {
    resizeIfNeeded(w, h);
    fillCB(w, h);
    buildDynamic(dynMesh);
    UINT nDyn = (UINT)std::min<size_t>(dynMesh.v.size(), MAX_DYN);
    if (nDyn) memcpy(dynPtr, dynMesh.v.data(), nDyn * sizeof(Vtx));
    nHud = 0; buildHud(w, h);

    UINT bi = swap->GetCurrentBackBufferIndex();
    alloc->Reset(); cl->Reset(alloc.Get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootSignature(rootSig.Get());
    cl->SetGraphicsRootConstantBufferView(0, cbRes->GetGPUVirtualAddress());
    cl->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW stView = { vbStatic->GetGPUVirtualAddress(), (UINT)(vbStatic->GetDesc().Width), sizeof(Vtx) };
    D3D12_VERTEX_BUFFER_VIEW dyView = { vbDyn->GetGPUVirtualAddress(), nDyn * (UINT)sizeof(Vtx), sizeof(Vtx) };
    D3D12_VERTEX_BUFFER_VIEW huView = { vbHud->GetGPUVirtualAddress(), (UINT)nHud * (UINT)sizeof(Vtx), sizeof(Vtx) };

    // ---- 1. flashlight shadow map
    D3D12_RESOURCE_BARRIER bars[3];
    bars[0] = transition(shadowTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    bars[1] = transition(sceneTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    bars[2] = transition(sceneDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cl->ResourceBarrier(3, bars);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvShadow = cpuAt(dsvHeap.Get(), dsvStep, 1), dsvScene = cpuAt(dsvHeap.Get(), dsvStep, 0), rtvScene = cpuAt(rtvHeap.Get(), rtvStep, 2);
    cl->OMSetRenderTargets(0, nullptr, FALSE, &dsvShadow);
    cl->ClearDepthStencilView(dsvShadow, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    if (g_flashIntensity > 0.001f) {
        D3D12_VIEWPORT vp = { 0, 0, (float)SHADOW_RES, (float)SHADOW_RES, 0, 1 }; D3D12_RECT sc = { 0, 0, SHADOW_RES, SHADOW_RES };
        cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
        cl->SetPipelineState(psoShadow.Get());
        drawChunks(true, stView);
        if (nDyn) { cl->IASetVertexBuffers(0, 1, &dyView); cl->DrawInstanced(nDyn, 1, 0, 0); }
    }
    D3D12_RESOURCE_BARRIER sb = transition(shadowTex.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &sb);

    // ---- 2. scene in HDR
    D3D12_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 }; D3D12_RECT sc = { 0, 0, w, h };
    cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
    cl->OMSetRenderTargets(1, &rtvScene, FALSE, &dsvScene);
    const float clr[4] = { 0.004f, 0.007f, 0.012f, 1 };
    cl->ClearRenderTargetView(rtvScene, clr, 0, nullptr);
    cl->ClearDepthStencilView(dsvScene, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cl->SetPipelineState(psoSky.Get());
    cl->DrawInstanced(3, 1, 0, 0);
    cl->SetPipelineState(psoScene.Get());
    drawChunks(false, stView);
    if (nDyn) { cl->IASetVertexBuffers(0, 1, &dyView); cl->DrawInstanced(nDyn, 1, 0, 0); }
    bars[0] = transition(sceneTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    bars[1] = transition(sceneDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    bars[2] = transition(backBuf[bi].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(3, bars);

    // ---- 3. camcorder post pass to the back buffer, then the on-screen display
    D3D12_CPU_DESCRIPTOR_HANDLE rtvBack = cpuAt(rtvHeap.Get(), rtvStep, (int)bi);
    cl->OMSetRenderTargets(1, &rtvBack, FALSE, nullptr);
    cl->SetPipelineState(psoPost.Get());
    cl->DrawInstanced(3, 1, 0, 0);
    if (nHud) {
        cl->SetPipelineState(psoHud.Get());
        cl->IASetVertexBuffers(0, 1, &huView);
        cl->DrawInstanced((UINT)nHud, 1, 0, 0);
    }
    if (wantShot) {
        D3D12_RESOURCE_DESC bd = backBuf[bi]->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 total = 0;
        dev->GetCopyableFootprints(&bd, 0, 1, 0, &fp, nullptr, nullptr, &total);
        readback.Reset(); makeBuffer(total, readback, nullptr, D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_BARRIER t = transition(backBuf[bi].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cl->ResourceBarrier(1, &t);
        D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
        src.pResource = backBuf[bi].Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        t = transition(backBuf[bi].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        cl->ResourceBarrier(1, &t);
        cl->Close();
        ID3D12CommandList* lists[] = { cl.Get() }; queue->ExecuteCommandLists(1, lists);
        waitGpu();
        void* mp = nullptr; D3D12_RANGE rr = { 0, (SIZE_T)total };
        if (SUCCEEDED(readback->Map(0, &rr, &mp))) { writeBmp(shotPath.c_str(), (const uint8_t*)mp + fp.Offset, w, h, fp.Footprint.RowPitch); readback->Unmap(0, nullptr); }
        wantShot = false;
        swap->Present(0, 0); waitGpu();
        return;
    }
    D3D12_RESOURCE_BARRIER pb = transition(backBuf[bi].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cl->ResourceBarrier(1, &pb);
    cl->Close();
    ID3D12CommandList* lists[] = { cl.Get() }; queue->ExecuteCommandLists(1, lists);
    swap->Present(shotPath.empty() ? 1 : 0, 0);
    waitGpu();
}

// ------------------------------------------------------------ window
static void setCursorHidden(bool hide) { if (hide == cursorHidden) return; cursorHidden = hide; ShowCursor(hide ? FALSE : TRUE); }

static void mouseLook() {
    bool active = g.state == ST_PLAY && GetForegroundWindow() == hwnd;
    setCursorHidden(active || g.state == ST_DEAD || g.state == ST_WIN);
    if (!active) { mouseArmed = false; return; }
    RECT rc; GetClientRect(hwnd, &rc);
    POINT c = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 }; ClientToScreen(hwnd, &c);
    POINT p; GetCursorPos(&p);
    if (mouseArmed) { g.mdx += (float)(p.x - c.x); g.mdy += (float)(p.y - c.y); }
    SetCursorPos(c.x, c.y);
    mouseArmed = true;
}

static void toggleFullscreen() {
    fullscreen = !fullscreen;
    if (fullscreen) {
        GetWindowRect(hwnd, &windowedRect);
        MONITORINFO mi = { sizeof mi }; GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_NOTOPMOST, windowedRect.left, windowedRect.top, windowedRect.right - windowedRect.left, windowedRect.bottom - windowedRect.top, SWP_FRAMECHANGED);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN: {
        bool repeat = (l & 0x40000000) != 0;
        if (w < 256) { g.keys[w] = true; if (!repeat) g.pressed[w] = true; }
        if (repeat) return 0;
        if (w == VK_ESCAPE) {
            if (g.state == ST_PLAY) g.state = ST_PAUSE; else if (g.state == ST_PAUSE) g.state = ST_PLAY; else if (g.state == ST_MENU) DestroyWindow(h);
        } else if (w == 'Q' && g.state == ST_PAUSE) DestroyWindow(h);
        else if (w == VK_RETURN) {
            if (g.state == ST_MENU || (g.state == ST_DEAD && g.deathT > 3.0f) || (g.state == ST_WIN && g.winT > 5.0f)) gameReset();
        } else if (w == VK_F11) toggleFullscreen();
        return 0; }
    case WM_KEYUP: if (w < 256) g.keys[w] = false; return 0;
    case WM_KILLFOCUS: ZeroMemory(g.keys, sizeof g.keys); if (g.state == ST_PLAY) g.state = ST_PAUSE; return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    // command line (for the screenshot tool)
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    float sx = 0, sz = 0, syaw = 0, spitch = 0; bool haveAt = false; int shotAfter = 0, slevel = 0, sstage = -1; float sfear = -1; int sent = 0; float sentD = 8; bool snv = false, sflashOff = false; float syBase = -1; int smode = 0;
    int winW = 1600, winH = 900;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto num = [&](int k) { return (i + k < argc) ? (float)_wtof(argv[i + k]) : 0.0f; };
        if (a == L"--shot" && i + 1 < argc) { char b[512]; WideCharToMultiByte(CP_ACP, 0, argv[i + 1], -1, b, 512, nullptr, nullptr); shotPath = b; i++; }
        else if (a == L"--frames") { shotAfter = (int)num(1); i++; }
        else if (a == L"--at") { sx = num(1); sz = num(2); syaw = num(3); spitch = num(4); haveAt = true; i += 4; }
        else if (a == L"--level") { slevel = (int)num(1); i++; }
        else if (a == L"--stage") { sstage = (int)num(1); i++; }
        else if (a == L"--ent") { sent = 1; sentD = num(1); i++; }
        else if (a == L"--nv") snv = true;
        else if (a == L"--menu") smode = 1;
        else if (a == L"--dead") smode = 2;
        else if (a == L"--win") smode = 3;
        else if (a == L"--lightoff") sflashOff = true;
        else if (a == L"--fear") { sfear = num(1); i++; }
        else if (a == L"--size") { winW = (int)num(1); winH = (int)num(2); i += 2; }
    }
    (void)syBase;
    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"HollowmereWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r = { 0, 0, winW, winH }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowW(wc.lpszClassName, L"HOLLOWMERE", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    RECT cr; GetClientRect(hwnd, &cr);
    if (!initD3D(std::max<int>(1, cr.right), std::max<int>(1, cr.bottom))) return 1;

    Au::init();
    buildWorld();
    gameInit();
    if (!uploadStatic()) return 1;
    ShowWindow(hwnd, show);

    if (!shotPath.empty()) {                                       // scripted still: skip the menu
        gameReset();
        g.fade = g.fadeT = 1;
        if (sstage >= 1) { g.stage = sstage; g.glow = g.glowT = 0; if (sstage == 2) { lamps[g.carLamp].on = 1; lamps[g.carLamp + 1].on = 1; } }
        if (haveAt) { g.pos = { sx, 0, sz }; g.level = slevel; g.pos.y = slevel ? F1 : (inMansion(sx, sz) ? F0 : groundH(sx, sz)); g.yaw = g.dyaw = syaw; g.pitch = g.dpitch = spitch; g.flashD = { cosf(syaw), 0, sinf(syaw) }; }
        if (sent) { V3 p = { g.pos.x + cosf(g.yaw) * sentD, 0, g.pos.z + sinf(g.yaw) * sentD }; entSpawn(p, slevel, MD_HUNT); g.e.yaw = atan2f(g.pos.x - p.x, g.pos.z - p.z); g.e.ttl = -1000; g.frozenSim = false; }
        g.nv = snv; if (sflashOff) g.flash = false;
        if (smode == 1) g.state = ST_MENU;
        if (smode == 2) { g.state = ST_DEAD; g.deathT = 4; g.fade = g.fadeT = 0.75f; g.fear = 1; g.staticT = 3; }
        if (smode == 3) { g.state = ST_WIN; g.winT = 6; g.fade = g.fadeT = 1; }
        g.introT = -1000;
        Au::master = 0;
    }

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    float acc = 0; const float STEP = 1 / 120.0f; int frame = 0;
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) goto done; TranslateMessage(&msg); DispatchMessageW(&msg); }
        mouseLook();
        QueryPerformanceCounter(&now);
        float dt = std::min(0.1f, (float)(now.QuadPart - prev.QuadPart) / freq.QuadPart);
        if (!shotPath.empty()) dt = 1 / 60.0f;
        prev = now; acc += dt;
        while (acc >= STEP) { gameUpdate(STEP); acc -= STEP; }
        gameCamera(dt);
        if (!shotPath.empty()) {
            if (sfear >= 0) { g.fear = sfear; }
            if (sent) { g.e.seenT = 0; g.e.ttl = -1000; }
            if (frame == shotAfter) wantShot = true;
        }
        GetClientRect(hwnd, &cr);
        if (cr.right > 0 && cr.bottom > 0) renderFrame(cr.right, cr.bottom); else Sleep(20);
        frame++;
        if (!shotPath.empty() && frame > shotAfter) goto done;
    }
done:
    waitGpu();
    Au::shutdown();
    CloseHandle(fenceEvent);
    return 0;
}
