// A simple(?) set of sliders related to controlling multimedia players.
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include "logger.h"
#include "drawnslider.h"

static constexpr char logModule[] =  "drawnslider";

// dr: drawrect electric floating boogaloo
// As the doc says, QPainter draws the right and bottom edges one pixel below
// and to the right of the expected location.  So provide a helper function
// that draws an adjusted rect to keep the main math clean.  As it turns out,
// we can avoid a whole host of bugs by using doubles rather than ints.
static void dr(QPainter *p, QRectF r) {
    QRectF r2(r.left() + 0.5, r.top() + 0.5, r.width() - 1, r.height() - 1);
    p->drawRect(r2);
}



DrawnSlider::DrawnSlider(QWidget *parent, QSize handle, QSize margin, int paddingHeight) :
    QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSliderGeometry(handle.width(), handle.height(),
                      margin.width(), margin.height(),
                      paddingHeight);
}

void DrawnSlider::setValue(double v)
{
    vValue = qBound(vMinimum, v, vMaximum);
    xPosition = valueToX(vValue);
    update();
}

void DrawnSlider::setMaximum(double v)
{
    if (vMaximum != v) {
        vMaximum = v;
        redrawBackground = true;
    }
    if (vValue > v)
        setValue(v);
    else
        update();
}

void DrawnSlider::setMinimum(double v)
{
    if (vMinimum != v) {
        vMinimum = v;
        redrawBackground = true;
    }
    if (vValue < v)
        setValue(v);
    else
        update();
}

double DrawnSlider::value()
{
    return vValue;
}

double DrawnSlider::maximum()
{
    return vMaximum;
}

double DrawnSlider::minimum()
{
    return vMinimum;
}

void DrawnSlider::setHighContrast(bool enabled)
{
    highContrast = enabled;
    redrawHandle = true;
    redrawBackground = true;
    update();
}

void DrawnSlider::applicationPaletteChanged()
{
    redrawHandle = true;
    redrawBackground = true;
    update();
}

void DrawnSlider::setSliderGeometry(int handleWidth, int handleHeight,
                                    int marginX, int marginY,
                                    int paddingHeight)
{
    this->handleWidth = handleWidth;
    this->handleHeight = handleHeight;
    this->marginX = marginX;
    this->marginY = marginY;
    this->paddingHeight = paddingHeight;

    setMinimumHeight(handleHeight+(paddingHeight*2));
    setMaximumHeight(handleHeight+(paddingHeight*2));
}

void DrawnSlider::handleHover(double x)
{
    // do nothing by default
    (void)x;
}

double DrawnSlider::valueToX(double value)
{
    double stride = sliderArea.right() - sliderArea.left();
    double x = sliderArea.left() + (((value-minimum()) * stride)
                                 / std::max(1.0, maximum() - minimum()));
    return qBound(sliderArea.left(),
                 x,
                 sliderArea.right() > sliderArea.left() ? sliderArea.right() : sliderArea.left());
}

double DrawnSlider::xToValue(double x)
{
    double stride = std::max(1.0, sliderArea.right() - sliderArea.left());
    double val = ((x-sliderArea.left()) * (maximum() - minimum()))
            / stride;
    return qBound(minimum(), val, maximum());
}

void DrawnSlider::refreshColors()
{
    QPalette pal = parentWidget() ? parentWidget()->palette() : palette();
    QColor text = pal.color(QPalette::Normal, QPalette::WindowText);
    progressColor = pal.color(QPalette::Normal, QPalette::Highlight);
    knobColor = progressColor.lighter(115);
    trackColor = text;
    trackColor.setAlphaF(highContrast ? 0.45 : 0.18);
    hoverColor = text;
    hoverColor.setAlphaF(highContrast ? 0.35 : 0.16);
    {
        if (highContrast) {
            grooveBorder = pal.color(QPalette::Normal, QPalette::Text);
            grooveFill   = pal.color(QPalette::Normal, QPalette::Window);
            handleBorder = pal.color(QPalette::Normal, QPalette::Text);
            handleFill   = pal.color(QPalette::Normal, QPalette::Window);
            bgColor      = pal.color(QPalette::Normal, QPalette::Window);
            loopColor    = pal.color(QPalette::Normal, QPalette::Highlight);
            markColor    = pal.color(QPalette::Normal, QPalette::Text);
        } else {
            grooveBorder = pal.color(QPalette::Normal, QPalette::Shadow);
            grooveFill   = pal.color(QPalette::Normal, QPalette::Base);
            handleBorder = pal.color(QPalette::Normal, QPalette::Button);
            handleFill   = pal.color(QPalette::Normal, QPalette::Button);
            bgColor      = pal.color(QPalette::Normal, QPalette::Window);
            loopColor    = pal.color(QPalette::Normal, QPalette::Highlight);
            markColor    = pal.color(QPalette::Normal, QPalette::Button);

            // is this a light theme?
            if (pal.color(QPalette::Normal, QPalette::Button).value() >
                    pal.color(QPalette::Normal, QPalette::Text).value()) {
                handleBorder = handleBorder.darker();
                markColor = markColor.darker();
            } else {
                handleBorder = handleBorder.lighter(200);
                markColor = markColor.lighter(200);
            }
        }
    }
}

void DrawnSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (redrawBackground || redrawHandle)
        refreshColors();

    if (redrawBackground) {
        makeBackground();
        redrawBackground = false;
    }
    if (redrawHandle) {
        makeHandle();
        redrawHandle = false;
    }

    QPainter p(this);
    qreal pr = devicePixelRatioF();
    p.scale(1.0/pr, 1.0/pr);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(0, 0, backgroundPic);
    p.setOpacity(isEnabled() ? 1.0 : 0.333);

    if (minimum() != maximum()) {
        double x = isDragging ? xPosition : valueToX(value());
        x -= handleWidth/2.0;
        double xpr = x * pr;
        int index = int((xpr - floor(xpr)) * 16.0) & 15;
        p.drawImage(int(xpr), int((height() - handleHeight)/2.0*pr), handlePics[index]);
    }
}

void DrawnSlider::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    /*
        MEDIA SLIDER CASE

                                          ^

                                        paddingHeight

                  <---- SLIDER AREA       v
  ^     +------------------------
        |
        |< marginX>
        |
        +------------------+-----   ^     ^
        |
        |                  |     marginY
        |
        |         +---------------  v
        |         |
height  |         |   GROOVE              hH
        |         |
        |         +---------------
        |               DRAWN
        |                  |
        |                AREA
        +------------------+-----         v
        |
        |
        |
  v     +------------------------
                                          ^

                                        paddingHeight

                                          v


        VOLUME SLIDER CASE

                  <---- SLIDER AREA
  ^     +------------------------
        |
        |<  hW/2  >
        |
        +------------------+-----     ^
        |
        |                  |
        |              DRAWN AREA
        |                  |
        |
height  |         +          ----    hH
        |                ----
        |            ----  |
        |        ----
        |    ----          |
        |----
        +------------------+----  ^   v
        |
        |                      padding
        |
  v     +-----------------------  v

    Therefore, "drawn area" is actually the control area, minus not needed
    padding.

    Therefore, "groove area" is actually the drawn area, minus any margin.

    Therefore, "slider area" is actually the drawn area, minus the handle
    area.

    Therefore, a volume slider ignores the groove area as it has no groove.
    However, it should provide 'margins' equal to half its handle size, as
    these relate to where the middle of the handle is.
    */

    grooveArea = QRectF(0, paddingHeight, width(), height() - (paddingHeight*2));
    drawnArea = grooveArea;
    grooveArea.adjust(marginX, marginY, -marginX, -marginY);
    sliderArea = grooveArea;
    double offset = handleWidth / 2.0 - marginX;
    sliderArea.adjust(offset, 0, -offset, 0);
    redrawHandle = true;
    redrawBackground = true;
}

void DrawnSlider::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange)
        redrawBackground = true;
    QWidget::changeEvent(event);
}

void DrawnSlider::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) {
        isDragging = true;
        xPosition = ev->position().x();
        setValue(xToValue(ev->position().x()));
        emit sliderMoved(value());
    }
}

void DrawnSlider::mouseReleaseEvent(QMouseEvent *ev)
{
    if (isDragging && ev->button() == Qt::LeftButton) {
        isDragging = false;
        update();
    }
}

void DrawnSlider::mouseMoveEvent(QMouseEvent *ev)
{
    if (isDragging && ev->buttons() & Qt::LeftButton) {
        double mouseValue = xToValue(ev->position().x());
        if (value() != mouseValue) {
            setValue(mouseValue);
            emit sliderMoved(value());
        }
    }
    handleHover(ev->position().x());
    // Forward mouse move events also to the parent widget
    ev->ignore();
}

MediaSlider::MediaSlider(QWidget *parent) :
    DrawnSlider(parent, QSize(16, 16), QSize(8, 0), 7)
{
}

// Modern seekbar: rounded track, filled progress, hover preview,
// chapter gaps and a round knob that grows on hover/drag.
void MediaSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (redrawBackground || redrawHandle) {
        refreshColors();
        redrawBackground = redrawHandle = false;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(isEnabled() ? 1.0 : 0.4);

    const bool active = hovering || isDragging;
    const double thickness = active ? 6.0 : 4.0;
    const double cy = height() / 2.0;
    const double left = sliderArea.left();
    const double right = std::max(left, sliderArea.right());
    const double radius = thickness / 2.0;
    const bool hasRange = minimum() != maximum();
    QRectF track(left, cy - radius, right - left, thickness);

    // Clip everything track-related to a rounded pill.
    QPainterPath pill;
    pill.addRoundedRect(track, radius, radius);
    p.save();
    p.setClipPath(pill);
    p.setPen(Qt::NoPen);

    p.fillRect(track, trackColor);

    double pos = hasRange ? (isDragging ? qBound(left, xPosition, right)
                                        : valueToX(value()))
                          : left;

    // Hover preview (lighter fill up to the mouse)
    if (hovering && hasRange && hoverX > pos)
        p.fillRect(QRectF(pos, track.top(), qBound(left, hoverX, right) - pos,
                          thickness), hoverColor);

    // Played portion
    if (hasRange)
        p.fillRect(QRectF(left, track.top(), pos - left, thickness), progressColor);

    // A-B loop region
    if (vLoopA >= 0 && vLoopB >= 0) {
        QColor lc = loopColor;
        lc.setAlphaF(0.55);
        double a = valueToX(vLoopA), b = valueToX(vLoopB);
        p.fillRect(QRectF(a, track.top(), b - a, thickness), lc);
    }

    // Chapter marks: small gaps in the track
    if (hasRange) {
        for (auto i = ticks.constBegin(); i != ticks.constEnd(); i++) {
            double x = valueToX(i.key());
            if (x <= left + 2.0 || x >= right - 2.0)
                continue;
            p.fillRect(QRectF(x - 1.0, track.top(), 2.0, thickness), bgColor);
        }
    }
    p.restore();

    // Single loop markers
    p.setPen(QPen(loopColor, 2.0, Qt::SolidLine, Qt::RoundCap));
    if (vLoopA >= 0 && vLoopB < 0) {
        double a = valueToX(vLoopA);
        p.drawLine(QPointF(a, cy - 7), QPointF(a, cy + 7));
    } else if (vLoopB >= 0 && vLoopA < 0) {
        double b = valueToX(vLoopB);
        p.drawLine(QPointF(b, cy - 7), QPointF(b, cy + 7));
    }

    // Knob
    if (hasRange && isEnabled()) {
        double r = active ? 7.5 : 5.5;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 90));
        p.drawEllipse(QPointF(pos, cy + 1.0), r + 1.0, r + 1.0);
        p.setBrush(knobColor);
        p.drawEllipse(QPointF(pos, cy), r, r);
        if (active) {
            p.setBrush(QColor(255, 255, 255, 230));
            p.drawEllipse(QPointF(pos, cy), r * 0.38, r * 0.38);
        }
    }
}

void MediaSlider::clearTicks()
{
    ticks.clear();
    vLoopA = vLoopB = -1;
    loopArea = { -1, -1, 0, 0 };
    redrawHandle = true;
    redrawBackground = true;
}

void MediaSlider::setTick(double value, QString text)
{
    ticks.insert(value, text);
    redrawBackground = true;
}

void MediaSlider::setLoopA(double a)
{
    vLoopA = a; updateLoopArea();
}

void MediaSlider::setLoopB(double b)
{
    vLoopB = b; updateLoopArea();
}

double MediaSlider::loopA()
{
    return vLoopA;
}

double MediaSlider::loopB() {
    return vLoopB;
}

void MediaSlider::resizeEvent(QResizeEvent *event)
{
    DrawnSlider::resizeEvent(event);
    updateLoopArea();
}

void MediaSlider::makeBackground()
{
    qreal pr = devicePixelRatioF();
    int pw = round(width() * pr);
    int ph = round(height() * pr);
    backgroundPic = QImage(pw, ph, QImage::Format_RGBA8888);
    backgroundPic.fill(Qt::transparent);
    QPainter p(&backgroundPic);
    p.scale(pr, pr);

    // Draw inside area
    p.setPen(Qt::NoPen);
    p.setBrush(grooveFill);
    dr(&p, grooveArea);

    // Draw any highlighted area
    p.setPen(loopColor);
    p.setBrush(loopColor);
    if (vLoopA >= 0 && vLoopB >= 0) {
        p.setBrush(loopColor);
        dr(&p, loopArea);
    } else if (vLoopA >= 0) {
        double pos = valueToX(vLoopA);
        p.drawLine(QPointF(pos + 0.5, grooveArea.top() + 1.5),
                   QPointF(pos + 0.5, grooveArea.bottom() - 1.5));
    } else if (vLoopB >= 0) {
        double pos = valueToX(vLoopB);
        p.drawLine(QPointF(pos + 0.5, grooveArea.top() + 1.5),
                    QPointF(pos + 0.5, grooveArea.bottom() - 1.5));

    }

    // Draw chapter marks
    p.setPen(markColor);
    for (auto i = ticks.constBegin(); i != ticks.constEnd(); i++) {
        double pos = valueToX(i.key());
        // Don't draw over the edge of the groove twice when disabled, so the
        // affected groove sides don't appear dark.
        if (isEnabled() || (pos > grooveArea.left() + 1.0 &&
                            pos < grooveArea.right() - 1.0)) {
            p.drawLine(QPointF(pos + 0.5, grooveArea.top() + 0.5),
                       QPointF(pos + 0.5, grooveArea.bottom() - 0.5));
        }
    }

    // Draw outside groove
    p.setPen(grooveBorder);
    p.setBrush(Qt::NoBrush);
    dr(&p, grooveArea);


}

void MediaSlider::makeHandle()
{
    qreal pr = devicePixelRatioF();
    int pw = round(handleWidth * pr);
    int ph = round(handleHeight * pr);

    for (int i = 0; i < 16; i++) {
        QImage handlePic(pw+1, ph, QImage::Format_ARGB32_Premultiplied);
        handlePic.fill(0);
        QPainter p(&handlePic);
        p.setRenderHint(QPainter::Antialiasing);
        p.setTransform(QTransform().translate(i/16.0,0).scale(pr,pr));
        QRectF slider(0, 0, handleWidth, handleHeight);
        slider.adjust(1.5, 1.5, -1.5, -1.5);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(handleBorder, 4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        dr(&p, slider);
        p.setPen(QPen(handleFill, 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        dr(&p, slider);
        handlePics[i] = handlePic;
    }
}

void MediaSlider::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    hovering = true;
    update();
    emit hoverBegin();
}

void MediaSlider::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    hovering = false;
    hoverX = -1;
    update();
    emit hoverEnd();
}

void MediaSlider::handleHover(double x)
{
    hoverX = x;
    update();
    double valueOfX = xToValue(x);
    emit hoverEnd();
    emit hoverValue(valueOfX, valueToTickText(valueOfX), x, isDragging);
}

void MediaSlider::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::MiddleButton && !ticks.isEmpty()) {
        double clickTime = xToValue(ev->position().x());
        auto it = ticks.lowerBound(clickTime);
        if (it == ticks.constEnd() || std::round(it.key()) > std::round(clickTime)) {
            --it;
        }
        emit middleClicked(it.key());
        return;
    } else if (ev->button() == Qt::LeftButton) {
        isDragging = true;
        handleHover(ev->position().x());
    }
    DrawnSlider::mousePressEvent(ev);
}

void MediaSlider::updateLoopArea()
{
    double left = valueToX(vLoopA);
    double right = valueToX(vLoopB);
    loopArea = {left, grooveArea.top() + 1, right - left, grooveArea.height() - 2};
    redrawBackground = true;
    update();
}

QString MediaSlider::valueToTickText(double value)
{
    QString last_text;

    double last_tick = -1;
    for (auto i = ticks.constBegin(); i != ticks.constEnd(); i++) {
        last_tick = i.key();
        if (last_tick > value)
            break;
        last_text = i.value();
    }
    return last_text;
}



VolumeSlider::VolumeSlider(QWidget *parent) :
    DrawnSlider(parent, QSize(14, 20), QSize(7, 10), 0)
{
}

void VolumeSlider::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    hovering = true;
    update();
}

void VolumeSlider::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    hovering = false;
    update();
}

// Modern volume slider: rounded track with fill and a round knob.
void VolumeSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (redrawBackground || redrawHandle) {
        refreshColors();
        redrawBackground = redrawHandle = false;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(isEnabled() ? 1.0 : 0.4);

    const double cy = height() / 2.0;
    const double left = sliderArea.left();
    const double right = std::max(left, sliderArea.right());
    const double thickness = 4.0;
    QRectF track(left, cy - thickness / 2.0, right - left, thickness);
    double pos = isDragging ? qBound(left, xPosition, right) : valueToX(value());

    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, thickness / 2.0, thickness / 2.0);
    p.setBrush(progressColor);
    p.drawRoundedRect(QRectF(left, track.top(), pos - left, thickness),
                      thickness / 2.0, thickness / 2.0);

    double r = (hovering || isDragging) ? 6.5 : 5.0;
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(QPointF(pos, cy + 1.0), r + 1.0, r + 1.0);
    p.setBrush(knobColor);
    p.drawEllipse(QPointF(pos, cy), r, r);
}

void VolumeSlider::makeBackground()
{
    qreal pr = devicePixelRatioF();
    int pw = round(drawnArea.width() * pr);
    int ph = round(drawnArea.height() * pr);
    backgroundPic = QImage(pw, ph, QImage::Format_RGBA8888);
    backgroundPic.fill(Qt::transparent);
    QPainter p(&backgroundPic);
    p.scale(pr, pr);

    double x1 = drawnArea.left() + 0.5;
    double y1 = drawnArea.top() + 0.5;
    double x2 = drawnArea.width() - 0.5;
    double y2 = drawnArea.height() - 0.5;
    QPointF groove[] = { QPointF(x1, y2), QPointF(x2, y2), QPointF(x2, y1) };

    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(grooveBorder);
    p.setBrush(grooveFill);
    p.drawConvexPolygon(groove, 3);
}

void VolumeSlider::makeHandle()
{
    qreal pr = devicePixelRatioF();
    int pw = round(handleWidth * pr);
    int ph = round(handleHeight * pr);
    for (int i = 0; i < 16; i++) {
        QImage handlePic(pw+1, ph, QImage::Format_ARGB32);
        handlePic.fill(0);
        QPainter p(&handlePic);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(pr, pr);
        p.translate(i/16.0,0);
        p.setBrush(handleFill);
        p.setPen(handleBorder);
        dr(&p, QRectF(0,0,handleWidth,handleHeight));
        handlePics[i] = handlePic;
    }
}
