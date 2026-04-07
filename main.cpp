#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Flex.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern "C" {
#include <xdo.h>
}

#include "emoji_data.hpp"
#include "theme.hpp"

static bool keep_alive = false;

/**
 * Encapsulates the logic for pasting an emoji into the previously active window.
 */
/**
 * Simulates a Ctrl+V key sequence to paste the current clipboard content.
 */
void paste_emoji() {
    if (xdo_t* xdo = xdo_new(NULL)) {
        xdo_send_keysequence_window(xdo, CURRENTWINDOW, "Control+v", 0);
        xdo_free(xdo);
    }
}

class EmojiGrid : public Fl_Widget {
    std::vector<int> filtered_indices;
    std::vector<Fl_Image*> images;
    int item_size = 44; // smaller item size (44x44)
    int img_size = 36;  // scaled image size
    int cols = 1;
    int selected_idx = 0;
    std::vector<std::string> lower_tags;

    static void idle_prep(void* data) {
        EmojiGrid* grid = (EmojiGrid*) data;
        for (size_t i = 0; i < ALL_EMOJIS.size(); ++i) {
            if (!grid->images[i]) {
                grid->prepare_image(i);
                return; // Give control back to UI
            }
        }
        Fl::remove_idle(idle_prep, data);
    }

    void prepare_image(int emoji_idx) {
        if (images[emoji_idx]) return;

        const auto& data = ALL_EMOJIS[emoji_idx];
        Fl_PNG_Image* img = new Fl_PNG_Image("mem", data.image_data, data.image_size);

        if (img && img->d() != 0) {
            // Use logical scaling (non-destructive in FLTK 1.4) to keep icons sharp when zoomed
            img->scale(img_size, img_size, 0, 1);
            images[emoji_idx] = img;
        } else {
            images[emoji_idx] = img; // Could be nullptr or failed image
        }
    }

public:
    EmojiGrid(int X, int Y, int W, int H): Fl_Widget(X, Y, W, H) {
        images.resize(ALL_EMOJIS.size(), nullptr);
        lower_tags.reserve(ALL_EMOJIS.size());
        for (size_t i = 0; i < ALL_EMOJIS.size(); ++i) {
            filtered_indices.push_back(i);
            std::string tags = ALL_EMOJIS[i].tags;
            std::transform(tags.begin(), tags.end(), tags.begin(), ::tolower);
            lower_tags.push_back(std::move(tags));
        }
        cols = W / item_size;
        if (cols < 1) cols = 1;
        int rows = (filtered_indices.size() + cols - 1) / cols;
        int expected_h = rows * item_size;
        Fl_Widget::resize(X, Y, W, expected_h);
        Fl::add_idle(idle_prep, this);
    }

    ~EmojiGrid() {
        for (auto img : images) {
            if (img) delete img;
        }
    }

    void filter(const std::string& query) {
        filtered_indices.clear();
        std::string q = query;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);

        for (size_t i = 0; i < ALL_EMOJIS.size(); ++i) {
            if (q.empty()) {
                filtered_indices.push_back(i);
                continue;
            }
            if (lower_tags[i].find(q) != std::string::npos) {
                filtered_indices.push_back(i);
            }
        }
        selected_idx = 0;

        cols = w() / item_size;
        if (cols < 1) cols = 1;
        int rows = (filtered_indices.size() + cols - 1) / cols;
        int expected_h = rows * item_size;
        Fl_Scroll* scroll = (Fl_Scroll*) parent();
        if (expected_h != h()) {
            if (scroll) scroll->redraw();
            Fl_Widget::size(w(), expected_h);
        }
        if (scroll) {
            scroll->scroll_to(0, 0); // scroll to top when filtering
            scroll->redraw();
        }
        redraw();
    }

    void resize(int X, int Y, int W, int H) override {
        if (X == x() && Y == y() && W == w() && H == h()) return;
        cols = W / item_size;
        if (cols < 1) cols = 1;
        int rows = (filtered_indices.size() + cols - 1) / cols;
        int expected_h = rows * item_size;
        Fl_Widget::resize(X, Y, W, expected_h);
    }

    void draw() override {
        fl_push_clip(x(), y(), w(), h());
        fl_color(color());
        fl_rectf(x(), y(), w(), h());

        Fl_Scroll* scroll = dynamic_cast<Fl_Scroll*>(parent());
        if (!scroll) return;
        int view_y = scroll->yposition();
        int view_h = scroll->h();

        int start_row = view_y / item_size;
        if (start_row < 0) start_row = 0;
        int end_row = (view_y + view_h + item_size - 1) / item_size;

        for (int r = start_row; r <= end_row; ++r) {
            for (int c = 0; c < cols; ++c) {
                int idx = r * cols + c;
                if (idx >= static_cast<int>(filtered_indices.size())) break;

                int real_idx = filtered_indices[idx];

                int item_x = x() + c * item_size;
                int item_y = y() + r * item_size;

                // Highlight selected only (background is already cleared by Line 141)
                if (idx == selected_idx) {
                    fl_color(FL_SELECTION_COLOR);
                    fl_rectf(item_x, item_y, item_size, item_size);
                }

                // Use pre-decoded images from cache
                if (!images[real_idx]) {
                    prepare_image(real_idx); // Emergency prepare if idle hadn't finished
                }

                if (images[real_idx] && (images[real_idx]->w() > 0 || images[real_idx]->d() != 0)) {
                    int img_x = item_x + (item_size - images[real_idx]->w()) / 2;
                    int img_y = item_y + (item_size - images[real_idx]->h()) / 2;
                    images[real_idx]->draw(img_x, img_y);
                }
            }
        }
        fl_pop_clip();
    }

    int handle(int event) override {
        switch (event) {
        case FL_MOVE: {
            int mx = Fl::event_x() - x();
            int my = Fl::event_y() - y();
            int c = mx / item_size;
            int r = my / item_size;

            if (c >= 0 && c < cols && r >= 0) {
                int hover_idx = r * cols + c;
                if (hover_idx < static_cast<int>(filtered_indices.size())) {
                    const auto& item = ALL_EMOJIS[filtered_indices[hover_idx]];
                    Fl_Tooltip::enter_area(this, x() + c * item_size, y() + r * item_size, item_size, item_size, item.name);
                    return 1;
                }
            }
            Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
            return 1;
        }
        case FL_ENTER:
            return 1; // Receive FL_MOVE
        case FL_LEAVE:
            Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
            return 1;
        case FL_PUSH: {
            int mx = Fl::event_x() - x();
            int my = Fl::event_y() - y();
            int c = mx / item_size;
            int r = my / item_size;

            if (c >= 0 && c < cols && r >= 0) {
                int clicked_idx = r * cols + c;
                if (clicked_idx < static_cast<int>(filtered_indices.size())) {
                    selected_idx = clicked_idx;
                    redraw();
                    do_callback();
                    return 1;
                }
            }
            break;
        }
        }
        return Fl_Widget::handle(event);
    }

    void set_selected_idx(int idx) {
        if (idx >= 0 && idx < static_cast<int>(filtered_indices.size())) {
            selected_idx = idx;
            const auto& item = ALL_EMOJIS[filtered_indices[selected_idx]];
            tooltip(item.name); // Update tooltip on keyboard navigation too

            int r = selected_idx / cols;
            int item_y = r * item_size;
            Fl_Scroll* scroll = (Fl_Scroll*) parent();
            if (item_y < scroll->yposition()) {
                scroll->scroll_to(0, item_y);
            } else if (item_y + item_size > scroll->yposition() + scroll->h()) {
                scroll->scroll_to(0, item_y + item_size - scroll->h());
            }
            redraw();
        }
    }

    int get_selected_idx() { return selected_idx; }
    int get_cols() { return cols; }
    int get_count() { return filtered_indices.size(); }

    const char* get_selected() {
        if (selected_idx >= 0 && selected_idx < static_cast<int>(filtered_indices.size())) {
            return ALL_EMOJIS[filtered_indices[selected_idx]].char_str;
        }
        return nullptr;
    }
};

/**
 * Finds and sets the emoji picker app icon from the embedded dataset.
 */
void setup_window_icon(Fl_Double_Window* win) {
    const unsigned char* icon_data = nullptr;
    size_t icon_size = 0;

    for (const auto& emoji : ALL_EMOJIS) {
        if (std::string_view(emoji.char_str) == "😂") {
            icon_data = emoji.image_data;
            icon_size = emoji.image_size;
            break;
        }
    }

    if (icon_data) {
        Fl_PNG_Image* app_icon = new Fl_PNG_Image("icon", icon_data, icon_size);
        if (app_icon && app_icon->d() != 0) {
            win->icon(app_icon);
        }
    }
}

class EmojiScroll : public Fl_Scroll {
public:
    EmojiScroll(int X, int Y, int W, int H): Fl_Scroll(X, Y, W, H) {}
    void resize(int X, int Y, int W, int H) override {
        Fl_Scroll::resize(X, Y, W, H);
        if (children() > 0) {
            Fl_Widget* grid = child(0);
            grid->resize(grid->x(), grid->y(), W - scrollbar.w(), grid->h());
        }
    }
};

class SearchInput : public Fl_Input {
    EmojiGrid* grid;

public:
    SearchInput(int X, int Y, int W, int H, const char* L = 0): Fl_Input(X, Y, W, H, L), grid(nullptr) {}
    void set_grid(EmojiGrid* g) { grid = g; }
    int handle(int e) override {
        if (e == FL_KEYDOWN && grid) {
            int key = Fl::event_key();
            int idx = grid->get_selected_idx();
            int cols = grid->get_cols();
            int count = grid->get_count();

            if (key == FL_Down) {
                if (idx + cols < count)
                    grid->set_selected_idx(idx + cols);
                else if (count > 0)
                    grid->set_selected_idx(count - 1);
                return 1;
            } else if (key == FL_Up) {
                if (idx >= cols)
                    grid->set_selected_idx(idx - cols);
                else
                    grid->set_selected_idx(0);
                return 1;
            } else if (key == FL_Right && insert_position() == mark() && insert_position() == size()) {
                if (idx < count - 1) grid->set_selected_idx(idx + 1);
                return 1;
            } else if (key == FL_Left && insert_position() == 0 && mark() == 0) {
                if (idx > 0) grid->set_selected_idx(idx - 1);
                return 1;
            } else if (key == FL_Enter) {
                grid->do_callback();
                return 1;
            } else if (key == FL_Escape) {
                exit(0);
            }
        }
        return Fl_Input::handle(e);
    }
};

void input_cb(Fl_Widget* w, void* data) {
    EmojiGrid* grid = (EmojiGrid*) data;
    SearchInput* input = (SearchInput*) w;
    grid->filter(input->value());
}

/**
 * Timeout callback to exit the background process after serving the clipboard.
 */
void exit_timeout_cb(void* data) {
    keep_alive = false;
}

void grid_cb(Fl_Widget* w, void* data) {
    EmojiGrid* grid = (EmojiGrid*) w;
    Fl_Double_Window* win = (Fl_Double_Window*) data;

    const char* emoji_char = grid->get_selected();
    if (!emoji_char) return;

    // Use native FLTK clipboard instead of spawning a subprocess
    Fl::copy(emoji_char, strlen(emoji_char), 1);

    win->hide();
    Fl::flush();

    // Delay to allow focus to return and clipboard to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    paste_emoji();

    // Do not exit(0) immediately; stay in background to serve the clipboard selection.
    // Exit after 10 seconds of inactivity or when the timeout is reached by clearing keep_alive.
    keep_alive = true;
    Fl::add_timeout(10.0, exit_timeout_cb);
}

struct PickerUI {
    SearchInput* input;
    EmojiGrid* grid;
};

/**
 * Constructs the main UI layout and returns handles to key widgets.
 */
PickerUI create_ui(Fl_Double_Window* win) {
    Fl_Flex* flex = new Fl_Flex(0, 0, win->w(), win->h());
    flex->type(Fl_Flex::COLUMN);
    flex->margin(5);
    flex->gap(5);

    SearchInput* input = new SearchInput(0, 0, 0, 0);
    input->when(FL_WHEN_CHANGED);
    input->textsize(16);
    flex->fixed(input, 30);

    EmojiScroll* scroll = new EmojiScroll(0, 0, 0, 0);
    scroll->box(FL_FLAT_BOX);
    scroll->type(Fl_Scroll::VERTICAL_ALWAYS);

    EmojiGrid* grid = new EmojiGrid(0, 0, win->w() - scroll->scrollbar.w(), win->h() - 40);
    scroll->end();

    flex->end();
    win->resizable(flex);

    input->set_grid(grid);
    input->callback(input_cb, grid);
    grid->callback(grid_cb, win);

    return {input, grid};
}

int main() {
    Fl::scheme("gtk+");
    fl_register_images();
    configure_fltk_colors();

    Fl_Double_Window* win = new Fl_Double_Window(380, 480, "Emoji Picker");
    setup_window_icon(win);

    PickerUI ui = create_ui(win);

    win->end();
    win->hotspot(win);
    win->show();

    ui.input->take_focus();

    while (keep_alive || Fl::first_window()) {
        Fl::wait(1.0);
    }

    return 0;
}
