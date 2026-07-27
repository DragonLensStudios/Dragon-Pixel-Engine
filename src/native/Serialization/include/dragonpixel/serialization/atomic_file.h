#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace dragonpixel::serialization
{
enum class save_fault
{
    none,
    after_temporary_flush,
};

struct save_result final
{
    bool succeeded{};
    std::string error;
};

struct utf8_transaction_write final
{
    std::filesystem::path target;
    std::string contents;
};

enum class transaction_save_fault
{
    none,
    after_staging,
    after_first_replace,
    leave_interrupted_after_first_replace,
    committed_journal_transient_sharing_violation,
    committed_journal_persistent_sharing_violation,
    committed_journal_transient_sharing_violation_then_topology_unavailable,
    committed_journal_reported_failure_after_publication,
    committed_journal_persistent_sharing_violation_then_transient_recovery_read,
    leave_interrupted_after_committed_journal,
    first_target_changed_after_prepared_journal,
    second_target_changed_after_first_replace,
    second_target_changed_during_publication,
    second_target_reported_failure_after_publication,
    second_target_changed_after_ambiguous_api_failure,
    second_target_inspection_unavailable_after_ambiguous_api_failure,
    committed_journal_candidate_after_primary_removal,
    committed_journal_transient_sharing_violation_then_primary_missing,
    second_target_missing_after_ambiguous_api_failure,
    leave_prepared_journal_candidate_after_primary_removal,
};

[[nodiscard]] save_result save_utf8_atomic(
    const std::filesystem::path& target,
    std::string_view contents,
    save_fault injected_fault = save_fault::none);

// Stages and flushes every document, its pre-image, and a versioned recovery
// journal before replacing any target. recovery_root must be an existing
// directory and every target plus its recovery artifacts must resolve inside
// it. A normal failure rolls the whole set back. The interruption fault seams
// intentionally leave either a prepared partial replacement or a committed
// transaction awaiting cleanup so a fresh recover_utf8_transactions call can
// exercise startup recovery.
[[nodiscard]] save_result save_utf8_transaction(
    std::span<const utf8_transaction_write> writes,
    const std::filesystem::path& recovery_root,
    transaction_save_fault injected_fault = transaction_save_fault::none);

// Recovers every valid prepared transaction contained below recovery_root and
// removes artifacts for transactions whose committed marker was durable.
// Recovery is conservative: malformed journals, escaped paths, unexpected
// target contents, and alias conflicts fail without overwriting those targets.
[[nodiscard]] save_result recover_utf8_transactions(
    const std::filesystem::path& recovery_root);

[[nodiscard]] save_result recover_backup(const std::filesystem::path& target);
}
