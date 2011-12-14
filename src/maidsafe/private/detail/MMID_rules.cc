/*
* ============================================================================
*
* Copyright [2011] maidsafe.net limited
*
* The following source code is property of maidsafe.net limited and is not
* meant for external use.  The use of this code is governed by the license
* file licence.txt found in the root of this directory and also on
* www.maidsafe.net.
*
* You are not free to copy, amend or otherwise use this source code without
* the explicit written permission of the board of directors of maidsafe.net.
*
* ============================================================================
*/

#include "maidsafe/private/chunk_action_authority.h"

#include "maidsafe/common/chunk_store.h"
#include "maidsafe/common/crypto.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/private/return_codes.h"
#include "maidsafe/private/log.h"


namespace maidsafe {

namespace priv {

DataType GetDataType(const std::string &name) {
  if (name.size() == crypto::SHA512::DIGESTSIZE)
    return kHashableSigned;

  if (name.size() == crypto::SHA512::DIGESTSIZE + 1) {
    DataType type(static_cast<DataType>(name.front()));
    if ((type > 0) && (type < kMaxDataType))
      return type;
  }

  DLOG(WARNING) << "Unknown data type " << static_cast<DataType>(name.front());
  return kUnknown;
}



ChunkActionAuthority::ChunkActionAuthority()
    : get_string_signal_(new GetStringSignalPtr::element_type),
      get_vector_signal_(new GetVectorSignalPtr::element_type) {}

ChunkActionAuthority::~ChunkActionAuthority() {}

ChunkActionAuthority::GetStringSignalPtr
    ChunkActionAuthority::get_string_signal() const {
  return get_string_signal_;
}

ChunkActionAuthority::GetVectorSignalPtr
    ChunkActionAuthority::get_vector_signal() const {
  return get_vector_signal_;
}

int ChunkActionAuthority::ProcessData(const OperationType &op_type,
                                      const std::string &name,
                                      const std::string &data,
                                      const asymm::PublicKey &public_key,
                                      ChunkStorePtr chunk_store) {
  if (op_type == ChunkActionAuthority::kHas)
    return chunk_store->Has(name) ? kKeyNotUnique : kKeyUnique;

  switch (GetDataType(name)) {
    case kAnmpid:
    case kMpid:
    case kHashableSigned:
      return ProcessSignedData(op_type, name, data, public_key, true,
                               chunk_store);
    case kNonHashableSigned:
      return ProcessSignedData(op_type, name, data, public_key, false,
                               chunk_store);
    case kMsid:
      return ProcessMsidData(op_type, name, data, public_key, chunk_store);
    case kMmid:
      return ProcessMmidData(op_type, name, data, public_key, chunk_store);
    default:
      return kUnknownFailure;
  }
}

int ChunkActionAuthority::ProcessSignedData(const OperationType &op_type,
                                            const std::string &name,
                                            const std::string &data,
                                            const asymm::PublicKey &public_key,
                                            const bool &hashable,
                                            ChunkStorePtr chunk_store) {
  if (PreOperationChecks(op_type, name, data, public_key, hashable) !=
      kSuccess) {
    DLOG(ERROR) << "PreOperationChecks failure.";
    return kPreOperationCheckFailure;
  }

  std::string current_data;
  switch (op_type) {

    case kStore:
      if (chunk_store->Has(name)) {
        DLOG(ERROR) << "Name of data exists. Use update.";
        return kDuplicateNameFailure;
      }
      if (!chunk_store->Store(name, data)) {
        DLOG(ERROR) << "ChunkStore Store failure.";
        return kStoreFailure;
      }
      break;

    case kDelete:
      if (VerifyCurrentData(name, public_key, chunk_store, &current_data) !=
          kSuccess) {
        DLOG(ERROR) << "VerifyCurrentData failure.";
        return kVerifyDataFailure;
      }
      if (!chunk_store->Delete(name)) {
        DLOG(ERROR) << "Error deleting packet";
        return kDeleteFailure;
      }
      break;

    case kUpdate:
      if (VerifyCurrentData(name, public_key, chunk_store, &current_data) !=
          kSuccess) {
        DLOG(ERROR) << "VerifyCurrentData failure.";
        return kVerifyDataFailure;
      }
      if (!chunk_store->Modify(name, data)) {
        DLOG(ERROR) << "Error Modifying packet";
        return kModifyFailure;
      }
      break;

    case kGet:
      if (VerifyCurrentData(name, public_key, chunk_store, &current_data) !=
          kSuccess) {
        DLOG(ERROR) << "VerifyCurrentData failure.";
        return kVerifyDataFailure;
      }
      (*get_string_signal_)(current_data);
      break;

    case kHas:
    default:
      DLOG(INFO) << "At this moment, code should not reach here.";
  }

  return kSuccess;
}

int ChunkActionAuthority::PreOperationChecks(const OperationType &op_type,
                                             const std::string &name,
                                             const std::string &data,
                                             const asymm::PublicKey &public_key,
                                             const bool &hashable) {
  if (op_type == kGet)
    return kSuccess;

  GenericPacket generic_packet;
  try {
    if (!generic_packet.ParseFromString(data)) {
      DLOG(ERROR) << "Data doesn't parse as a GenericPacket";
      return kInvalidSignedData;
    }
  }
  catch(const std::exception &e) {
    DLOG(ERROR) << "Data doesn't parse as a GenericPacket: " << e.what();
    return kInvalidSignedData;
  }

  if (hashable) {
    if (op_type == kUpdate) {
      DLOG(ERROR) << "No update of hashable data allowed";
      return kInvalidUpdate;
    }
    if (crypto::Hash<crypto::SHA512>(
            generic_packet.data() + generic_packet.signature()) != name) {
      DLOG(ERROR) << "Marked hashable, doesn't hash";
      return kNotHashable;
    }
  }
  
  if (asymm::CheckSignature(generic_packet.data(), generic_packet.signature(),
                            public_key) != kSuccess) {
    DLOG(ERROR) << "Signature verification failed";
    return kSignatureVerificationFailure;
  }

  return kSuccess;
}

int ChunkActionAuthority::VerifyCurrentData(const std::string &name,
                                            const asymm::PublicKey &public_key,
                                            ChunkStorePtr chunk_store,
                                            std::string *current_data) {
  *current_data = chunk_store->Get(name);
  if (current_data->empty()) {
    DLOG(ERROR) << "VerifyCurrentData - Failure to get data";
    return kVerifyDataFailure;
  }

  GenericPacket generic_packet;
  try {
    if (!generic_packet.ParseFromString(*current_data)) {
      DLOG(ERROR) << "Data doesn't parse as a GenericPacket";
      return kInvalidSignedData;
    }
  }
  catch(const std::exception &e) {
    DLOG(ERROR) << "Data doesn't parse as a GenericPacket: " << e.what();
    return kInvalidSignedData;
  }

  if (asymm::ValidateKey(public_key) &&
      asymm::CheckSignature(generic_packet.data(),
                            generic_packet.signature(),
                            public_key) != kSuccess) {
    DLOG(ERROR) << "VerifyCurrentData - Not owner of packet";
    return kNotOwner;
  }

  return kSuccess;
}

int ChunkActionAuthority::ProcessMsidData(const OperationType &op_type,
                                          const std::string &name,
                                          const std::string &data,
                                          const asymm::PublicKey &public_key,
                                          ChunkStorePtr chunk_store) {
  std::string current_data(chunk_store->Get(name));
  bool already_exists(true);
  if (current_data.empty()) {
    DLOG(INFO) << "No such MSID";
    already_exists = false;
  }

  if (already_exists) {
    DataWrapper data_wrapper;
    if (!data_wrapper.ParseFromString(current_data)) {
      DLOG(ERROR) << "current MSID corrupted";
      return kParseFailure;
    }

    MSID current_msid;
    if (!current_msid.ParseFromString(
            data_wrapper.signed_data().data())) {
      DLOG(ERROR) << "current MSID corrupted";
      return kParseFailure;
    }

    if (asymm::CheckSignature(current_msid.public_key(),
                              current_msid.signature(),
                              public_key) != 0) {
      DLOG(INFO) << "Not owner, can only store MCID or get keys from MSID";
      if (op_type == kStore) {
        if (current_msid.accepts_new_contacts()) {
          current_msid.add_encrypted_mcid(data.signed_data().data());
          data_wrapper.mutable_signed_data()->set_data(
              current_msid.SerializeAsString());
          if (!chunk_store->Modify(name,
                                   data_wrapper.SerializeAsString())) {
            DLOG(ERROR) << "Failed to add MCID";
            return kModifyFailure;
          }
        } else {
          DLOG(INFO) << "Not accepting MCIDs";
          return kWontAcceptContact;
        }
      } else if (op_type == kGet) {
        GenericPacket gp;
        gp.set_data(current_msid.public_key());
        gp.set_signature(current_msid.signature());
        gp.set_type(kMsid);
        (*get_string_signal_)(gp.SerializeAsString());
      } else {
        DLOG(ERROR) << "Forbidden operation";
        return kUnknownFailure;
      }
    } else {
      switch (op_type) {
        case kGet:
          if (current_msid.encrypted_mcid_size() > 0) {
            std::vector<std::string> mcids;
            for (int n(0); n != current_msid.encrypted_mcid_size(); ++n)
              mcids.push_back(current_msid.encrypted_mcid(n));
            current_msid.clear_encrypted_mcid();
            data_wrapper.mutable_signed_data()->set_data(
                current_msid.SerializeAsString());
            if (!chunk_store->Modify(
                    name,
                    data_wrapper.SerializeAsString())) {
              DLOG(ERROR) << "Failed to modify after geting MCIDs";
              return kModifyFailure;
            }
            (*get_vector_signal_)(mcids);
          }
          break;
        case kUpdate:
          /***
            * If owner, change the allowance of storage.
            * Other ops in the future?
            ***/
            break;
        case kDelete:
          // Delete the whole thing
          if (!chunk_store->Delete(name)) {
            DLOG(ERROR) << "Failure to delete value";
            return kDeleteFailure;
          }
          /************** or all messages
          MSID mmid;
          msid.Parse(current_data);
          msid.clear_encrypted_mcid();
          ******************************/
        default: return kUnknownFailure;
      }
    }
  } else {
    // Storing the whole thing
    MSID wrapper_msid;
    if (!wrapper_msid.ParseFromString(data.signed_data().data())) {
      DLOG(ERROR) << "Data doesn't parse";
      return kStoreFailure;
    }

    if (asymm::CheckSignature(wrapper_msid.public_key(),
                              wrapper_msid.signature(),
                              public_key) != 0) {
      DLOG(ERROR) << "Failed validation of data";
      return kStoreFailure;
    }

    std::string a(data.SerializeAsString());
    if (!chunk_store->Store(name, a)) {
      DLOG(ERROR) << "Failed committing to chunk store";
      return kStoreFailure;
    }
  }

  return kSuccess;
}

int ChunkActionAuthority::ProcessMmidData(const OperationType &op_type,
                                          const std::string &name,
                                          const std::string &data,
                                          const asymm::PublicKey &public_key,
                                          ChunkStorePtr chunk_store) {
  // Check existance
  std::string current_data(chunk_store->Get(name));
  bool already_exists(true);
  if (current_data.empty()) {
    DLOG(INFO) << "No such MMID";
    already_exists = false;
  }

  // Check ownership
    // not owner, store message, no checks
    // owner, get messages, delete, store initially
  if (already_exists) {
    DataWrapper data_wrapper;
    if (!data_wrapper.ParseFromString(current_data)) {
      DLOG(ERROR) << "current MMID - DataWrapper corrupted";
      return kParseFailure;
    }

    MMID current_mmid;
    if (!current_mmid.ParseFromString(
            data_wrapper.signed_data().data())) {
      DLOG(ERROR) << "current MMID corrupted";
      return kParseFailure;
    }

    if (asymm::CheckSignature(current_mmid.public_key(),
                              current_mmid.signature(),
                              public_key) != 0) {
      DLOG(INFO) << "Not owner, can only store Encrypted or get keys from MMID "
                 << Base32Substr(name);
      if (op_type == kStore) {
        if (!current_mmid.add_encrypted_message()->ParseFromString(
            data.signed_data().data())) {
          DLOG(ERROR) << "Failed to parse Encrypted";
          return kModifyFailure;
        }
        data_wrapper.mutable_signed_data()->set_data(
            current_mmid.SerializeAsString());
        if (!chunk_store->Modify(name,
                                 data_wrapper.SerializeAsString())) {
          DLOG(ERROR) << "Failed to add MCID";
          return kModifyFailure;
        }
      } else if (op_type == kGet) {
        GenericPacket gp;
        gp.set_data(current_mmid.public_key());
        gp.set_signature(current_mmid.signature());
        gp.set_type(kMmid);
        (*get_string_signal_)(gp.SerializeAsString());
      } else {
        DLOG(ERROR) << "Forbidden operation";
        return kUnknownFailure;
      }
    } else {
      switch (op_type) {
        case kGet:
//            (*get_string_signal_)(current_data);
          if (current_mmid.encrypted_message_size() > 0) {
            std::vector<std::string> mmids;
            for (int n(0); n != current_mmid.encrypted_message_size(); ++n)
              mmids.push_back(
                  current_mmid.encrypted_message(n).SerializeAsString());
            current_mmid.clear_encrypted_message();
            data_wrapper.mutable_signed_data()->set_data(
                current_mmid.SerializeAsString());
            if (!chunk_store->Modify(
                    name,
                    data_wrapper.SerializeAsString())) {
              DLOG(ERROR) << "Failed to modify after geting MCIDs";
              return kModifyFailure;
            }
            (*get_vector_signal_)(mmids);
          }
          break;
        case kDelete:
          // Delete the whole thing
          if (!chunk_store->Delete(name)) {
            DLOG(ERROR) << "Failure to delete value";
            return kDeleteFailure;
          }
          /************** or all messages
          MMID mmid;
          mmid.Parse(current_data);
          mmid.clear_encrypted_message();
          ******************************/
        default: return kUnknownFailure;
      }
    }
  } else {
    // Storing the whole thing
    MMID wrapper_msid;
    if (!wrapper_msid.ParseFromString(data.signed_data().data())) {
      DLOG(ERROR) << "Data doesn't parse";
      return kStoreFailure;
    }

    if (asymm::CheckSignature(wrapper_msid.public_key(),
                              wrapper_msid.signature(),
                              public_key) != 0) {
      DLOG(ERROR) << "Failed validation of data";
      return kStoreFailure;
    }

    std::string a(data.SerializeAsString());
    if (!chunk_store->Store(name, a)) {
      DLOG(ERROR) << "Failed committing to chunk store";
      return kStoreFailure;
    } else {
      DLOG(ERROR) << "Stored MMID: " << Base32Substr(name);
    }
  }

  return kSuccess;
}

}  // namespace priv

}  // namespace maidsafe

