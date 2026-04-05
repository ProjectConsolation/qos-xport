#pragma once

#include <array>
#include <string>

namespace build_info
{
	namespace detail
	{
#if __has_include("version.hpp")
#include "version.hpp"
#define QOS_XPORT_HAS_REVISION 1
#else
#define QOS_XPORT_HAS_REVISION 0
#define REVISION 0
#endif

		inline int month_from_string(const std::string& month)
		{
			static const std::array<std::string, 12> months =
			{
				"Jan", "Feb", "Mar", "Apr", "May", "Jun",
				"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
			};

			for (size_t i = 0; i < months.size(); ++i)
			{
				if (months[i] == month)
				{
					return static_cast<int>(i + 1);
				}
			}

			return 0;
		}
	}

	inline std::string get_version_tag()
	{
#if QOS_XPORT_HAS_REVISION
#ifdef _DEBUG
		return "r" + std::to_string(REVISION) + "-debug";
#else
		return "r" + std::to_string(REVISION);
#endif
#else
		const std::string date = __DATE__;
		const std::string time = __TIME__;
		const auto month = detail::month_from_string(date.substr(0, 3));
		const auto day = std::stoi(date.substr(4, 2));
		const auto year = std::stoi(date.substr(7, 4));
		const auto hour = std::stoi(time.substr(0, 2));
		const auto minute = std::stoi(time.substr(3, 2));

		char buffer[64]{};
#ifdef _DEBUG
		sprintf_s(buffer, "build-%04d%02d%02d-%02d%02d-debug", year, month, day, hour, minute);
#else
		sprintf_s(buffer, "build-%04d%02d%02d-%02d%02d", year, month, day, hour, minute);
#endif
		return buffer;
#endif
	}

	inline std::string get_window_title()
	{
		return "QoS-xport [" + get_version_tag() + "]";
	}
}



