// ui_widget_factory.h
#pragma once
#include "ui_widgets.h"

class UIWidgetFactory {
public:
    static IHeader*    CreateHeader(DisplayType type);
    static IListView*  CreateList(DisplayType type);
    static IParamControl* CreateParam(DisplayType type);
};
