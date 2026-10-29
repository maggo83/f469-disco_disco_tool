import udisplay as _udisplay

from udisplay import update, on, off, set_rotation

if hasattr(_udisplay, "transition_begin"):
    clear_framebuffers = _udisplay.clear_framebuffers
    transition_abort = _udisplay.transition_abort
    transition_active = _udisplay.transition_active
    transition_begin = _udisplay.transition_begin
    transition_start = _udisplay.transition_start
    TRANSITION_HORIZONTAL_PUSH_IN = (
        _udisplay.TRANSITION_HORIZONTAL_PUSH_IN
    )
    TRANSITION_HORIZONTAL_PUSH_OUT = (
        _udisplay.TRANSITION_HORIZONTAL_PUSH_OUT
    )
    TRANSITION_HORIZONTAL_SLIDE_IN = (
        _udisplay.TRANSITION_HORIZONTAL_SLIDE_IN
    )
    TRANSITION_HORIZONTAL_SLIDE_OUT = (
        _udisplay.TRANSITION_HORIZONTAL_SLIDE_OUT
    )
    TRANSITION_VERTICAL_SLIDE_IN = _udisplay.TRANSITION_VERTICAL_SLIDE_IN
    TRANSITION_VERTICAL_SLIDE_OUT = _udisplay.TRANSITION_VERTICAL_SLIDE_OUT
    EASING_LINEAR = _udisplay.EASING_LINEAR
    EASING_EASE_IN_CUBIC = _udisplay.EASING_EASE_IN_CUBIC
    EASING_EASE_OUT_CUBIC = _udisplay.EASING_EASE_OUT_CUBIC
    EASING_EASE_IN_OUT_CUBIC = _udisplay.EASING_EASE_IN_OUT_CUBIC
    EASING_EASE_OUT_QUINT = _udisplay.EASING_EASE_OUT_QUINT

del _udisplay

def init(autoupdate=True):
    import udisplay
    udisplay.init()

    if autoupdate:
        import micropython
        import pyb
        def schedule(t):
            """Try to schedule an LED update"""
            try:
                micropython.schedule(udisplay.update, 30)
            except:
                pass
        timer = pyb.Timer(4) # timer 4
        timer.init(freq=30)  # 30Hz update rate
        timer.callback(schedule)