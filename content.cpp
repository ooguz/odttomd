#include "content.h"

#include <algorithm>
#include <iostream>
#include <stack>
#include <vector>
#include <boost/scope_exit.hpp>
#include <expat.h>
#include "expat_utils.h"
#include "numbering.h"
#include "util.h"

namespace {

class Context {

public:
	Context(const Styles &styles)
		: styles(styles)
	{
	}

public:
	const Styles &styles;
	std::stack<const Style *> appliedStyles;
	uint32_t listLevel = 0;
	std::string currentUrl;
	std::vector<uint32_t> currentOutlineNumbering;

};

void writeEscapedChar(const char c) {
	switch (c) {
		case '<':
			std::cout << "&lt;";
			break;
		case '>':
			std::cout << "&gt;";
			break;
		case '\\':
		case '`':
		case '*':
		case '_':
		case '{':
		case '}':
		case '[':
		case ']':
		case '(':
		case ')':
		case '#':
		case '+':
		case '-':
		case '.':
		case '!':
			std::cout << '\\';
		default:
			std::cout << c;
	}
}

void writeEscapedString(const std::string &str) {
	for (const char &c : str) {
		::writeEscapedChar(c);
	}
}

void onStart(void *userData, const XML_Char *name, const XML_Char **atts) {
	Context *context = static_cast<Context *>(userData);

	if (! ::strcmp(name, "text:h")) {
		uint32_t level = ::attrUint(atts, "text:outline-level", 0);

		std::cout << std::string(std::max(level, static_cast<uint32_t>(1)), '#') << ' ';

		if (level > 0) {
			while (context->currentOutlineNumbering.size() > level) {
				context->currentOutlineNumbering.pop_back();
			}
			while (context->currentOutlineNumbering.size() < level - 1) {
				uint32_t addedLevel = static_cast<uint32_t>(context->currentOutlineNumbering.size()) + 1;
				context->currentOutlineNumbering.push_back(context->styles.getOutlineLevelStyle(addedLevel).startValue);
			}

			const OutlineLevelStyle &outlineLevelStyle = context->styles.getOutlineLevelStyle(level);
			if (context->currentOutlineNumbering.size() < level) {
				context->currentOutlineNumbering.push_back(outlineLevelStyle.startValue);
			} else {
				++(context->currentOutlineNumbering.back());
			}

			uint32_t currentNumber = context->currentOutlineNumbering.back();

			::writeEscapedString(outlineLevelStyle.prefix);
			uint32_t fromLevel = 1;
			if (outlineLevelStyle.displayLevels <= level) {
				fromLevel = 1 + level - outlineLevelStyle.displayLevels;
			} else {
				std::cerr << "More levels to display than the current level: " << outlineLevelStyle.displayLevels << " > " << level << std::endl;
			}

			for (uint32_t higherLevel = fromLevel; higherLevel < level; ++higherLevel) {
				const OutlineLevelStyle &higherLevelStyle = context->styles.getOutlineLevelStyle(higherLevel);
				if (! higherLevelStyle.numFormat.empty()) {
					::writeEscapedString(numbering::createNumber(context->currentOutlineNumbering[higherLevel - 1], higherLevelStyle.numFormat, higherLevelStyle.numLetterSync));
				}
				::writeEscapedChar('.');
			}
			if (! outlineLevelStyle.numFormat.empty()) {
				::writeEscapedString(numbering::createNumber(currentNumber, outlineLevelStyle.numFormat, outlineLevelStyle.numLetterSync));
			}
			::writeEscapedString(outlineLevelStyle.suffix);

			std::cout << ' ';
		}
	}
	if (! ::strcmp(name, "text:list")) {
		++(context->listLevel);
	}
	if (! ::strcmp(name, "text:list-item")) {
		std::cout << std::string(context->listLevel - 1, ' ') << '*' << ' ';
	}
	if (! ::strcmp(name, "text:span")) {
		const Style &style = context->styles.getStyle(::attrString(atts, "text:style-name", ""));
		context->appliedStyles.push(&style);
		if (style.bold) {
			std::cout << "__";
		}
	}
	if (! ::strcmp(name, "text:a")) {
		context->currentUrl = ::attrString(atts, "xlink:href", "");
		std::cout << '[';
	}
	if (! ::strcmp(name, "text:line-break")) {
		std::cout << '\n';
	}
}

void onEnd(void *userData, const XML_Char *name) {
	Context *context = static_cast<Context *>(userData);

	if (! ::strcmp(name, "text:h")) {
		std::cout << '\n';
		std::cout << '\n';
	}
	if (! ::strcmp(name, "text:p")) {
		std::cout << '\n';

		if (context->listLevel == 0)
			std::cout << '\n';
	}
	if (! ::strcmp(name, "text:list")) {
		--(context->listLevel);

		if (context->listLevel == 0)
			std::cout << '\n';
	}
	if (! ::strcmp(name, "text:span")) {
		const Style &style = *context->appliedStyles.top();
		context->appliedStyles.pop();

		if (style.bold) {
			std::cout << "__";
		}
	}
	if (! ::strcmp(name, "text:a")) {
		std::cout << ']';
		std::cout << '(';
		::writeEscapedString(context->currentUrl);
		std::cout << ')';
	}
}

void onData(void *, const XML_Char *s, int len) {
	for (const XML_Char *c = s; c < s + len; ++c) {
		::writeEscapedChar(*c);
	}
}

}

void parseContent(zip_file *f, const Styles &styles) {
	Context context(styles);

	XML_Parser parser = ::XML_ParserCreate(nullptr);
	BOOST_SCOPE_EXIT(&parser) {
		::XML_ParserFree(parser);
	} BOOST_SCOPE_EXIT_END

	::XML_SetUserData(parser, &context);
	::XML_SetElementHandler(parser, &onStart, &onEnd);
	::XML_SetCharacterDataHandler(parser, &onData);

	static const size_t bufferSize = 4 * 1024;
	char buffer[bufferSize];

	int64_t readResult = 0;

	while ((readResult = zip_fread(f, buffer, bufferSize)) > 0) {
		if (::XML_Parse(parser, buffer, static_cast<int>(readResult), false) == 0)
			throw 11;
	}

	if (readResult < 0) {
		throw 10;
	}

	if (::XML_Parse(parser, buffer, 0, true) == 0)
		throw 12;
}
