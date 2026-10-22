// RoomMap.hpp — a top-down view of StruckRoom's 2-D room for DPF NanoVG UIs: the room drawn at its
// width:depth shape with a one-wavelength grid, the struck object and the listener as markers you can
// drag, and the walls drawn heavier as they absorb more and glowing when they ring.
//
// The markers ARE the Src X/Y and Mic X/Y parameters: dragging reports through the Callback as
// fractions of the room, 0..1, and the UI turns that into ordinary edits, so automation records it.
// x runs left to right (left is the left channel), y from the front wall at the bottom.

#ifndef STRUCKROOM_ROOMMAP_HPP
#define STRUCKROOM_ROOMMAP_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DGL

class RoomMap : public NanoSubWidget
{
public:
    enum Marker { kObject = 0, kMic = 1, kNone = -1 };

    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void roomMapDragStarted(int marker) = 0;
        virtual void roomMapMoved(int marker, float x, float y) = 0;   // fractions of the room
        virtual void roomMapDragFinished(int marker) = 0;
    };

    RoomMap(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback) { fHasFont = loadSharedResources(); }

    void setRoom(float width, float depth)          { fW = width; fD = depth; repaint(); }
    void setObject(float x, float y)                { fObjX = x; fObjY = y; repaint(); }
    void setMic(float x, float y)                   { fMicX = x; fMicY = y; repaint(); }
    // Per pair (0 = side walls, 1 = front/back): absorption 0..1 and ring level (0 = silent).
    void setWall(int pair, float absorb, float ring) { fAbsorb[pair] = absorb; fRing[pair] = ring; repaint(); }
    void setColors(Color object, Color mic, Color side, Color frontBack)
    {
        fObjCol = object; fMicCol = mic; fWallCol[0] = side; fWallCol[1] = frontBack;
    }
    void setUiScale(float s) { fUiScale = s; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight(), s = fUiScale;

        beginPath(); roundedRect(0, 0, w, h, 6); fillColor(Color(46, 40, 22)); fill();
        beginPath(); roundedRect(0.5f, 0.5f, w - 1, h - 1, 6);
        strokeColor(Color(80, 70, 42)); strokeWidth(1.0f); stroke();

        float rx, ry, rw, rh;
        roomRect(rx, ry, rw, rh);

        // One-wavelength grid: the scale everything else is measured in.
        const float perLambda = rw / std::max(fW, 0.01f);
        if (perLambda >= 6.0f)
        {
            beginPath();
            for (float x = perLambda; x < rw - 0.5f; x += perLambda) { moveTo(rx + x, ry); lineTo(rx + x, ry + rh); }
            for (float y = perLambda; y < rh - 0.5f; y += perLambda) { moveTo(rx, ry + rh - y); lineTo(rx + rw, ry + rh - y); }
            strokeColor(Color(72, 64, 40)); strokeWidth(1.0f); stroke();
        }

        // Walls: side walls are the left and right edges, front/back the bottom and top. Heavier as
        // they absorb more; a soft glow under them when they ring.
        auto wall = [&](float x0, float y0, float x1, float y1, int pair) {
            if (fRing[pair] > 0.0f)
            {
                Color glow = fWallCol[pair]; glow.alpha = std::min(0.45f, 0.18f + 0.15f * fRing[pair]);
                beginPath(); moveTo(x0, y0); lineTo(x1, y1);
                strokeColor(glow); strokeWidth((6.0f + 6.0f * fRing[pair]) * s); stroke();
            }
            Color c = fWallCol[pair]; c.alpha = 0.55f + 0.45f * fAbsorb[pair];
            beginPath(); moveTo(x0, y0); lineTo(x1, y1);
            strokeColor(c); strokeWidth((1.5f + 6.0f * fAbsorb[pair]) * s); stroke();
        };
        wall(rx, ry, rx, ry + rh, 0);
        wall(rx + rw, ry, rx + rw, ry + rh, 0);
        wall(rx, ry + rh, rx + rw, ry + rh, 1);
        wall(rx, ry, rx + rw, ry, 1);

        // Markers.
        float ox, oy, mx, my;
        toPixels(fObjX, fObjY, ox, oy);
        toPixels(fMicX, fMicY, mx, my);
        const float r = 7.0f * s;

        beginPath(); moveTo(ox, oy); lineTo(mx, my);   // the direct path
        Color path = fObjCol; path.alpha = 0.35f;
        strokeColor(path); strokeWidth(1.0f); stroke();

        beginPath(); circle(ox, oy, r); fillColor(fObjCol); fill();
        if (fDrag == kObject || fHover == kObject)
        {
            beginPath(); circle(ox, oy, r + 3.0f * s); strokeColor(fObjCol); strokeWidth(1.5f); stroke();
        }

        // The listener is a figure-8 facing the side walls plus an omni: two lobes and a centre.
        Color lobe = fMicCol; lobe.alpha = 0.5f;
        beginPath(); circle(mx - r * 0.9f, my, r * 0.75f); circle(mx + r * 0.9f, my, r * 0.75f);
        strokeColor(lobe); strokeWidth(1.5f); stroke();
        beginPath(); circle(mx, my, r * 0.6f); fillColor(fMicCol); fill();
        if (fDrag == kMic || fHover == kMic)
        {
            beginPath(); circle(mx, my, r * 2.2f); strokeColor(fMicCol); strokeWidth(1.5f); stroke();
        }

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f * s);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(fObjCol); text(ox + r + 3 * s, oy + 2 * s, "object", nullptr);
            fillColor(fMicCol); text(mx + r * 2.2f + 2 * s, my + 2 * s, "listener", nullptr);

            char size[48];
            std::snprintf(size, sizeof(size), "%.2f x %.2f wavelengths  (grid: 1 wavelength)", fW, fD);
            fillColor(Color(150, 154, 162));
            textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
            text(10 * s, h - 6 * s, size, nullptr);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(fWallCol[0]); text(10 * s, 6 * s, "side walls", nullptr);
            fillColor(fWallCol[1]); text(90 * s, 6 * s, "front / back", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (ev.press)
        {
            const int m = hit(ev.pos.getX(), ev.pos.getY());
            if (m == kNone) return false;
            fDrag = m;
            fCallback->roomMapDragStarted(fDrag);
            dragTo(ev.pos.getX(), ev.pos.getY());
            return true;
        }
        if (fDrag != kNone)
        {
            const int m = fDrag;
            fDrag = kNone;
            fCallback->roomMapDragFinished(m);
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fDrag != kNone) { dragTo(ev.pos.getX(), ev.pos.getY()); return true; }
        const int m = hit(ev.pos.getX(), ev.pos.getY());
        if (m != fHover) { fHover = m; repaint(); }
        return false;
    }

private:
    // The room, fitted to the widget at its own width:depth shape, leaving a margin for the labels.
    void roomRect(float& x, float& y, float& w, float& h) const
    {
        const float s = fUiScale;
        const float availW = getWidth() - 40.0f * s, availH = getHeight() - 56.0f * s;
        const float aspect = std::max(fW, 0.01f) / std::max(fD, 0.01f);
        w = availW; h = w / aspect;
        if (h > availH) { h = availH; w = h * aspect; }
        x = (getWidth() - w) * 0.5f;
        y = 26.0f * s + (availH - h) * 0.5f;
    }

    void toPixels(float fx, float fy, float& px, float& py) const
    {
        float x, y, w, h;
        roomRect(x, y, w, h);
        px = x + fx * w;
        py = y + (1.0f - fy) * h;
    }

    int hit(double px, double py) const
    {
        float ox, oy, mx, my;
        toPixels(fObjX, fObjY, ox, oy);
        toPixels(fMicX, fMicY, mx, my);
        const float reach = 16.0f * fUiScale;
        const float dObj = std::hypot(float(px) - ox, float(py) - oy);
        const float dMic = std::hypot(float(px) - mx, float(py) - my);
        if (dObj > reach && dMic > reach) return kNone;
        return dObj <= dMic ? kObject : kMic;
    }

    // Not moveTo: that would hide NanoVG's path command of the same name.
    void dragTo(double px, double py)
    {
        float x, y, w, h;
        roomRect(x, y, w, h);
        const float fx = std::clamp(float((px - x) / std::max(w, 1.0f)), 0.0f, 1.0f);
        const float fy = std::clamp(1.0f - float((py - y) / std::max(h, 1.0f)), 0.0f, 1.0f);
        if (fDrag == kObject) { fObjX = fx; fObjY = fy; } else { fMicX = fx; fMicY = fy; }
        fCallback->roomMapMoved(fDrag, fx, fy);
        repaint();
    }

    Callback* fCallback;
    float fW = 3.0f, fD = 2.2f;
    float fObjX = 0.31f, fObjY = 0.37f, fMicX = 0.62f, fMicY = 0.71f;
    float fAbsorb[2] = { 0.15f, 0.15f }, fRing[2] = { 0.0f, 0.0f };
    Color fObjCol { 120, 190, 140 }, fMicCol { 230, 230, 236 };
    Color fWallCol[2] = { Color(226, 120, 110), Color(120, 200, 170) };
    int   fDrag = kNone, fHover = kNone;
    float fUiScale = 1.0f;
    bool  fHasFont = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoomMap)
};

END_NAMESPACE_DGL

#endif // STRUCKROOM_ROOMMAP_HPP
