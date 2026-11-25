#pragma once

#include <string>
#include <string_view>

#include "core/log.hpp"
#include "core/result.hpp"

namespace moonlight::core {

/// Format de sortie d'un message de log.
enum class LogFormat {
    Text,  ///< Une ligne lisible : "[  12.345] [INFO ] [categorie] message cle=valeur"
    Json,  ///< JSON Lines : un objet JSON par ligne (exploitable par des outils).
};

/// "text" ou "json".
const char* logFormatName(LogFormat format) noexcept;

/// Convertit un nom de format (insensible a la casse) ; retourne false si inconnu.
bool parseLogFormat(std::string_view name, LogFormat& out) noexcept;

/// Formate un message SANS retour a la ligne final.
///
/// Texte : les valeurs de champs vides ou contenant espace, '"', '=', '\\' ou
/// un caractere de controle sont entre guillemets avec echappements. Le message
/// lui-meme est ecrit tel quel.
///
/// JSON : {"uptime":1.500,"level":"info","category":"c","message":"m","fields":{"k":"v"}}
/// Les chaines sont echappees selon la RFC 8259. Les octets >= 0x80 sont
/// recopies tels quels : fournissez de l'UTF-8 valide.
std::string formatLogRecord(const LogRecord& record, LogFormat format);

/// Sink ecrivant chaque message (une ligne) sur stderr.
LogSink makeStderrSink(LogFormat format);

/// Sink ajoutant chaque message (une ligne) a la fin du fichier `path`
/// (cree s'il n'existe pas ; le dossier parent doit exister).
///
/// Chaque ligne est ecrite d'un seul bloc sous verrou puis vidangee : les lignes
/// de threads differents ne s'entremelent pas. Les erreurs d'ecriture
/// ulterieures sont ignorees (un journal ne peut pas se signaler lui-meme).
/// Le fichier est ferme quand le dernier exemplaire du sink est detruit.
Result<LogSink> makeFileSink(std::string_view path, LogFormat format);

}  // namespace moonlight::core
