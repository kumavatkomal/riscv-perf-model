#include "edm/adapters/whisper/WhisperEDMAdapter.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "sparta/utils/SpartaAssert.hpp"

namespace olympia::edm
{
    namespace
    {
        constexpr size_t INVALID_COLUMN = static_cast<size_t>(-1);

        std::string normalizeHeader_(const std::string & header)
        {
            auto begin = std::find_if_not(header.begin(), header.end(),
                                          [](unsigned char ch) { return std::isspace(ch) != 0; });
            auto end = std::find_if_not(header.rbegin(), header.rend(),
                                        [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
            std::string out(begin, end);
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return out;
        }

        size_t findColumnIndex_(const std::vector<std::string> & headers,
                                const std::string & target)
        {
            const std::string normalized_target = normalizeHeader_(target);
            for(size_t i = 0; i < headers.size(); ++i)
            {
                if(normalizeHeader_(headers[i]) == normalized_target)
                {
                    return i;
                }
            }

            return INVALID_COLUMN;
        }

        uint32_t instSizeBytes_(uint32_t opcode)
        {
            return ((opcode & 0x3) == 0x3) ? 4U : 2U;
        }

        int32_t signExtend_(uint32_t value, unsigned bits)
        {
            const uint32_t shift = 32U - bits;
            return static_cast<int32_t>(value << shift) >> shift;
        }

        bool decodeBranchTarget_(Addr pc, uint32_t opcode, Addr & out_target)
        {
            if((opcode & 0x3) != 0x3)
            {
                return false; // Compressed or invalid; no target decode.
            }

            const uint32_t op = opcode & 0x7F;
            if(op == 0x63)
            {
                // B-type: imm[12|10:5|4:1|11]
                uint32_t imm = 0;
                imm |= ((opcode >> 31) & 0x1) << 12;
                imm |= ((opcode >> 25) & 0x3F) << 5;
                imm |= ((opcode >> 8) & 0xF) << 1;
                imm |= ((opcode >> 7) & 0x1) << 11;
                const int32_t simm = signExtend_(imm, 13);
                out_target = pc + static_cast<int64_t>(simm);
                return true;
            }

            if(op == 0x6F)
            {
                // JAL: imm[20|10:1|11|19:12]
                uint32_t imm = 0;
                imm |= ((opcode >> 31) & 0x1) << 20;
                imm |= ((opcode >> 21) & 0x3FF) << 1;
                imm |= ((opcode >> 20) & 0x1) << 11;
                imm |= ((opcode >> 12) & 0xFF) << 12;
                const int32_t simm = signExtend_(imm, 21);
                out_target = pc + static_cast<int64_t>(simm);
                return true;
            }

            return false;
        }

        bool parseNextPcFromModifiedRegs_(const std::string & text, uint64_t & out_pc)
        {
            const std::string needle = "pc=";
            const auto pos = text.find(needle);
            if(pos == std::string::npos)
            {
                return false;
            }

            const size_t start = pos + needle.size();
            size_t end = text.find_first_of(";,", start);
            if(end == std::string::npos || end < start)
            {
                end = text.size();
            }

            const std::string value = text.substr(start, end - start);
            if(value.empty())
            {
                return false;
            }

            out_pc = std::strtoull(value.c_str(), nullptr, 16);
            return true;
        }

        void parseInstInfo_(const std::string & raw_info,
                            bool & is_branch,
                            bool & is_conditional,
                            bool & is_taken,
                            bool & is_load,
                            bool & is_store)
        {
            std::string info = raw_info;
            std::transform(info.begin(), info.end(), info.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            const auto first = info.find_first_not_of(" \t\n\r");
            if(first == std::string::npos)
            {
                info.clear();
            }
            else
            {
                const auto last = info.find_last_not_of(" \t\n\r");
                info = info.substr(first, last - first + 1);
            }

            is_branch = false;
            is_conditional = false;
            is_taken = false;
            is_load = false;
            is_store = false;

            if(info == "t" || info == "nt")
            {
                is_branch = true;
                is_conditional = true;
                is_taken = (info == "t");
                return;
            }

            if(info == "j" || info == "c" || info == "r")
            {
                is_branch = true;
                is_conditional = false;
                is_taken = true;
                return;
            }

            if(info == "l")
            {
                is_load = true;
                return;
            }

            if(info == "s")
            {
                is_store = true;
                return;
            }

            if(info == "a")
            {
                is_load = true;
                is_store = true;
                return;
            }
        }
    }

    WhisperEDMAdapter::WhisperEDMAdapter(const std::string & /*config_file*/,
                                         const std::string & workload,
                                         const EDMBackendFactory::BackendParams & params) :
        workload_(workload),
        params_(params),
        whisper_bin_(getEnvOrDefault_("OLYMPIA_WHISPER_BIN", "whisper")),
        whisper_args_(getEnvOrDefault_("OLYMPIA_WHISPER_ARGS", "")),
        keep_trace_csv_(parseBool_(getEnvOrDefault_("OLYMPIA_WHISPER_KEEP_CSV", "0"), false))
    {
        if(const auto it = params_.find("whisper_bin");
           it != params_.end() && !trim_(it->second).empty())
        {
            whisper_bin_ = trim_(it->second);
        }

        if(const auto it = params_.find("whisper_args"); it != params_.end())
        {
            whisper_args_ = trim_(it->second);
        }

        if(const auto it = params_.find("whisper_keep_csv"); it != params_.end())
        {
            keep_trace_csv_ = parseBool_(it->second, keep_trace_csv_);
        }

        loadWhisperTrace_();
    }

    WhisperEDMAdapter::~WhisperEDMAdapter()
    {
        if(!keep_trace_csv_ && !trace_csv_path_.empty())
        {
            (void) ::unlink(trace_csv_path_.c_str());
        }
    }

    bool WhisperEDMAdapter::isFinished(CoreId, HartId) const
    {
        return finished_;
    }

    Addr WhisperEDMAdapter::peekNextPc(CoreId, HartId) const
    {
        return next_pc_;
    }

    InstructionInfo WhisperEDMAdapter::step(CoreId, HartId)
    {
        sparta_assert(cursor_ < trace_.size(),
                      "Whisper step requested after trace end");

        InstructionInfo info = trace_[cursor_];
        ++cursor_;

        if(cursor_ < trace_.size())
        {
            next_pc_ = trace_[cursor_].pc;
            info.next_pc = next_pc_;
        }
        else
        {
            next_pc_ = info.pc + 4;
            info.next_pc = next_pc_;
            finished_ = true;
        }

        if(!info.alt_next_pc.has_value())
        {
            info.alt_next_pc = info.next_pc;
        }

        if(cursor_ >= trace_.size())
        {
            finished_ = true;
        }

        return info;
    }

    InstructionInfo WhisperEDMAdapter::stepWithOverridePc(CoreId core_id,
                                                           HartId hart_id,
                                                           Addr override_pc)
    {
        if(seekToPc_(override_pc))
        {
            next_pc_ = override_pc;
            finished_ = false;
        }

        return step(core_id, hart_id);
    }

    void WhisperEDMAdapter::commitInstruction(CoreId, HartId, uint64_t)
    {
    }

    void WhisperEDMAdapter::commitStoreWrite(CoreId, HartId, uint64_t)
    {
    }

    void WhisperEDMAdapter::dropStoreWrite(CoreId, HartId, uint64_t)
    {
    }

    void WhisperEDMAdapter::flush(CoreId, HartId, const EDMCheckpoint & checkpoint)
    {
        const bool rewound = seekToPc_(checkpoint.correct_path_pc);

        if(!rewound)
        {
            sparta_assert(false,
                          "Whisper flush failed: unable to seek to checkpoint PC=0x"
                          << std::hex << checkpoint.correct_path_pc << std::dec);
        }

        next_pc_ = checkpoint.correct_path_pc;
        finished_ = false;
    }

    void WhisperEDMAdapter::loadWhisperTrace_()
    {
        runWhisperToCsv_();
        parseTraceCsv_();

        sparta_assert(!trace_.empty(),
                      "Whisper adapter produced an empty trace for workload='"
                      << workload_ << "'");

        cursor_ = 0;
        next_pc_ = trace_.front().pc;
        finished_ = false;
    }

    void WhisperEDMAdapter::runWhisperToCsv_()
    {
        char temp_path[] = "/tmp/olympia_whisper_trace_XXXXXX";
        const int temp_fd = ::mkstemp(temp_path);
        sparta_assert(temp_fd >= 0,
                      "Failed to create temporary Whisper trace file template: "
                      << std::strerror(errno));
        ::close(temp_fd);

        trace_csv_path_ = temp_path;

        std::stringstream cmd;
        cmd << shellQuote_(whisper_bin_)
            << " --logfile " << shellQuote_(trace_csv_path_)
            << " --csvlog ";

        if(!whisper_args_.empty())
        {
            cmd << whisper_args_ << " ";
        }

        cmd << shellQuote_(workload_);

        const int rc = std::system(cmd.str().c_str());
        sparta_assert(rc == 0,
                      "Whisper command failed with exit code " << rc
                      << ". Command: " << cmd.str()
                      << ". Set OLYMPIA_WHISPER_BIN/OLYMPIA_WHISPER_ARGS as needed.");
    }

    void WhisperEDMAdapter::parseTraceCsv_()
    {
        std::ifstream csv(trace_csv_path_);
        sparta_assert(csv.good(),
                      "Failed to open Whisper CSV trace at '" << trace_csv_path_ << "'");

        std::string line;
        sparta_assert(static_cast<bool>(std::getline(csv, line)),
                      "Whisper CSV trace missing header row: '" << trace_csv_path_ << "'");

        const auto headers = splitCsvLine_(line);
        const size_t pc_idx = findColumnIndex_(headers, "pc");
        const size_t inst_idx = findColumnIndex_(headers, "inst");
        const size_t opcode_idx = findColumnIndex_(headers, "opcode");
        const size_t modified_regs_idx = findColumnIndex_(headers, "modified regs");
        const size_t inst_info_idx = findColumnIndex_(headers, "inst info");

        sparta_assert(pc_idx != INVALID_COLUMN,
                      "Whisper CSV missing required 'pc' column in '"
                      << trace_csv_path_ << "'");
        const size_t opcode_source_idx = (inst_idx != INVALID_COLUMN) ? inst_idx : opcode_idx;
        sparta_assert(opcode_source_idx != INVALID_COLUMN,
                  "Whisper CSV missing required 'inst' (or 'opcode') column in '"
                  << trace_csv_path_ << "'");

        trace_.clear();
        pc_to_indices_.clear();

        while(std::getline(csv, line))
        {
            const auto cols = splitCsvLine_(line);
            if(cols.size() <= std::max(pc_idx, opcode_source_idx))
            {
                continue;
            }

            InstructionInfo info;
            info.iss_uid = trace_.size() + 1;
            info.pc = parseInteger_(cols[pc_idx]);
            info.opcode = static_cast<uint32_t>(parseInteger_(cols[opcode_source_idx]));

            bool is_branch = false;
            bool is_conditional = false;
            bool is_taken = false;
            bool is_load = false;
            bool is_store = false;
            if(inst_info_idx != INVALID_COLUMN && inst_info_idx < cols.size())
            {
                parseInstInfo_(cols[inst_info_idx], is_branch, is_conditional, is_taken,
                               is_load, is_store);
            }

            uint64_t branch_target = 0;
            bool has_branch_target = false;
            if(modified_regs_idx != INVALID_COLUMN && modified_regs_idx < cols.size())
            {
                has_branch_target = parseNextPcFromModifiedRegs_(cols[modified_regs_idx],
                                                                 branch_target);
            }

            Addr decoded_target = 0;
            bool has_decoded_target = false;
            if(!has_branch_target)
            {
                has_decoded_target = decodeBranchTarget_(info.pc, info.opcode, decoded_target);
                if(has_decoded_target)
                {
                    branch_target = decoded_target;
                }
            }

            const uint32_t inst_size = instSizeBytes_(info.opcode);
            const Addr seq_pc = info.pc + inst_size;

            info.is_branch = is_branch;
            info.is_taken = is_taken;
            info.is_load = is_load;
            info.is_store = is_store;
            info.is_wrong_path = false;

            info.opcode_size = inst_size;

            info.next_pc = seq_pc;
            info.alt_next_pc.reset();

            if(is_branch)
            {
                if(is_conditional)
                {
                    if(is_taken)
                    {
                        if(has_branch_target)
                        {
                            info.next_pc = branch_target;
                            info.alt_next_pc = seq_pc;
                        }
                    }
                    else
                    {
                        info.next_pc = seq_pc;
                        if(has_branch_target)
                        {
                            info.alt_next_pc = branch_target;
                        }
                    }
                }
                else
                {
                    if(has_branch_target)
                    {
                        info.next_pc = branch_target;
                        info.alt_next_pc = seq_pc;
                    }
                }
            }

            pc_to_indices_[info.pc].push_back(trace_.size());
            trace_.emplace_back(info);
        }
    }

    bool WhisperEDMAdapter::seekToPc_(Addr pc)
    {
        const auto it = pc_to_indices_.find(pc);
        if(it == pc_to_indices_.end() || it->second.empty())
        {
            return false;
        }

        const auto & indices = it->second;
        const auto lb = std::lower_bound(indices.begin(), indices.end(), cursor_);
        if(lb != indices.end())
        {
            cursor_ = *lb;
            return true;
        }

        cursor_ = indices.front();
        return true;
    }

    std::string WhisperEDMAdapter::getEnvOrDefault_(const char * env_name,
                                                    const std::string & default_value)
    {
        const char * raw = std::getenv(env_name);
        if(raw == nullptr)
        {
            return default_value;
        }

        const std::string value = trim_(raw);
        if(value.empty())
        {
            return default_value;
        }

        return value;
    }

    bool WhisperEDMAdapter::parseBool_(const std::string & value, bool default_value)
    {
        const std::string normalized = trim_(value);
        if(normalized.empty())
        {
            return default_value;
        }

        std::string lowered = normalized;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });

        if(lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
        {
            return true;
        }

        if(lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
        {
            return false;
        }

        return default_value;
    }

    std::string WhisperEDMAdapter::shellQuote_(const std::string & text)
    {
        std::string out;
        out.reserve(text.size() + 2);
        out.push_back('\'');

        for(const char ch : text)
        {
            if(ch == '\'')
            {
                out += "'\\''";
            }
            else
            {
                out.push_back(ch);
            }
        }

        out.push_back('\'');
        return out;
    }

    std::string WhisperEDMAdapter::trim_(const std::string & text)
    {
        const auto first = std::find_if_not(text.begin(), text.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; });

        if(first == text.end())
        {
            return "";
        }

        const auto last = std::find_if_not(text.rbegin(), text.rend(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }).base();

        return std::string(first, last);
    }

    uint64_t WhisperEDMAdapter::parseInteger_(const std::string & text)
    {
        const std::string cleaned = trim_(text);
        sparta_assert(!cleaned.empty(), "Cannot parse empty integer from Whisper CSV field");

        const uint64_t value = std::strtoull(cleaned.c_str(), nullptr, 16);
        return value;
    }

    std::vector<std::string> WhisperEDMAdapter::splitCsvLine_(const std::string & line)
    {
        std::vector<std::string> cols;
        std::string current;
        bool in_quotes = false;

        for(size_t i = 0; i < line.size(); ++i)
        {
            const char ch = line[i];
            if(ch == '"')
            {
                if(in_quotes && (i + 1) < line.size() && line[i + 1] == '"')
                {
                    current.push_back('"');
                    ++i;
                }
                else
                {
                    in_quotes = !in_quotes;
                }
            }
            else if(ch == ',' && !in_quotes)
            {
                cols.emplace_back(trim_(current));
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }

        cols.emplace_back(trim_(current));
        return cols;
    }

    static olympia::edm::BackendRegistrar<olympia::edm::WhisperEDMAdapter>
        s_whisper_registrar("whisper");

    void forceWhisperLink() {}
}
