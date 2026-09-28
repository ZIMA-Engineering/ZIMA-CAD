#pragma once
#include <zima/symbols/definition.hpp>
#include <zima/document/part_document.hpp>

namespace zima::symbols {
// The native Part owns editing history and Body identities. Definition is the
// insertion snapshot derived from that document, never a replacement for it.
[[nodiscard]] document::PartDocument native_document(const Definition&);
[[nodiscard]] Definition native_definition(const document::PartDocument&, bool insertion_snapshot = true);
}
