#pragma once

namespace Window {
class SessionController;
}

class ChannelData;

namespace Ayu::ChannelPopular {

void Show(
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel);

}
