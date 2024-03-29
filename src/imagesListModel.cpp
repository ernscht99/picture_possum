//
// Created by felix on 3/2/23.
//

#include "imagesListModel.h"

namespace {
    std::string get_sha1(const std::unique_ptr<char[]>& buffer, size_t length) {
        unsigned char digest[20];
        SHA1(reinterpret_cast<const unsigned char *>(buffer.get()), length, digest);
        std::ostringstream readable;
        readable << std::hex;
        for (unsigned char c : digest){
            readable << static_cast<int>(c);
        }
        return readable.str();
    }
    QString from_timestamp(time_t timestamp) {
        char time_string_buffer[64];
        tm time{};
        ::localtime_r(&timestamp, &time);
        ::strftime(time_string_buffer, 64,"%Y-%m-%d", &time);
        return QString{time_string_buffer};
    }
}

namespace possum{
    using namespace std;
    using namespace std::filesystem;

    void ImagesListModel::load_images(const std::string &directory_path) {
        for (const auto &dir_entry : directory_iterator(directory_path)){
            ImageType file_type{parse_extension(dir_entry.path())};

            ///For each file in directory, see if it is an image file we want (valid type)
            if (dir_entry.is_regular_file() && settings.valid_types.contains(file_type)){
                ///Read file into memory to calculate hash
                size_t file_size = dir_entry.file_size();
                unique_ptr<char[]> buffer = make_unique<char[]>(file_size);
                ifstream file_stream(dir_entry.path(), ios::binary | ios::in);
                file_stream.read(buffer.get(), static_cast<long>(file_size));
                get_exif_date(file_stream);
                ///Calculate hash and push Image object to vector that is to be returned
                time_t estimated_date = estimate_date(file_stream, dir_entry.path().filename().string());
                insert_image({dir_entry.path(), get_sha1(buffer, file_size), file_type, estimated_date});
            }
        }
        emit layoutChanged();
    }

    QVariant ImagesListModel::data(const QModelIndex &index, int role) const {
        if (!index.isValid())
            return {};
        if (role == DataView)
            return {QString::fromStdString(hash_handles.at(index.row()))};
        if (role == Qt::FontRole) {
            if (index.column() == 2)
                return {QFont{"Emoji"}};
        }
        if (role == Qt::DisplayRole) {
            if (index.column() == 0) {
                std::string file_name{image_map.find(hash_handles.at(index.row()))->second->getFilename()};
                return {QString::fromStdString(file_name)};
            }
            if (index.column() == 1) {
                time_t timestamp = image_map.find(hash_handles.at(index.row()))->second->getCreationTime();
                return timestamp ? from_timestamp(timestamp) : QString{"No date"};
            }
            if (index.column() == 2) {
                auto tag_ids = image_map.find(hash_handles.at(index.row()))->second->getTagIds();
                stringstream builder;
                for (const auto & tag_id : tag_ids){
                    builder << settings.render_tag_symbol(tag_id);
                    builder << " ";
                }
                return QString::fromStdString(builder.str());
            }
        }
        return {};
    }

    QVariant ImagesListModel::headerData(int section, Qt::Orientation orientation, int role) const {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Horizontal)
            switch (section){
            case 0:
                return { QString("File Name") };
            case 1:
                return {QString("Time")};
            case 2:
                return {QString("Tags")};
            default:
                return {};
            }
        return {};
    }

    int ImagesListModel::rowCount(const QModelIndex &parent) const {
        return static_cast<int>(hash_handles.size());
    }
    int ImagesListModel::columnCount(const QModelIndex &parent) const {
        return 3;
    }

    ImagesListModel::ImagesListModel(Settings settings, QObject *parent) : QAbstractTableModel(parent), settings(std::move(settings)) {
    }


    void ImagesListModel::insert_image(const Image& inserting_image) {
        auto image_iter ( image_map.find(inserting_image.getSha1Sum()));
        if(image_iter == image_map.end()){
            image_map.insert({inserting_image.getSha1Sum(), std::make_unique<Image>(inserting_image)});
            hash_handles.emplace_back(inserting_image.getSha1Sum());
        } else {
            Image * existing_image = image_iter->second.get();
            existing_image->add_path(inserting_image.getPath());
            if (inserting_image.getCreationTime() < existing_image->getCreationTime())
                image_iter->second->setCreationTime(inserting_image.getCreationTime());
        }
    }

    Image ImagesListModel::get_image(const string &hash) {
        auto to_get = image_map.find(hash);
        if (to_get == image_map.end())
            return Image{};
        return *to_get->second;
    }


    string ImagesListModel::get_exif_date(ifstream &file_stream) {
        file_stream.seekg(0);
        TinyEXIF::EXIFInfo exif_data(file_stream);
        return exif_data.DateTimeOriginal;
    }

    time_t
    ImagesListModel::estimate_date(std::ifstream &file_stream, const std::string &file_name) {
        time_t epoch = 0;

        ///Try to get date from exif first, if it is there.
        tm estimated_date = *::gmtime(&epoch);
        string exif_date = get_exif_date(file_stream);
        if (! exif_date.empty()) {
            strptime(exif_date.c_str(), "%Y:%m:%d %H:%M:%S", &estimated_date);
            return ::mktime(&estimated_date);
        }

        ///If there is no EXIF, try to extract from filename according to set format strings
        size_t first_digit = file_name.find_first_of("0123456789");
        if (first_digit == string::npos)
            first_digit = 0;

        for (const QString& format : settings.date_conversion_formats) {
            if (strptime(file_name.c_str() + first_digit, format.toStdString().c_str(), &estimated_date)) {
                epoch = ::mktime(&estimated_date);
                break;
            }
        }
        ///Little safeguard against dates after 2100
        if (epoch > 4109223911)
            return 0;

        return epoch;
    }

    void ImagesListModel::sort(int column, Qt::SortOrder order) {
        auto compare = [this, &order, &column](const string& a, const string& b) {
            auto first = this->image_map.find(a);
            auto second = this->image_map.find(b);
            if (order == Qt::DescendingOrder) {
                swap(first, second);
            }
            switch(column) {
                case 0:
                    return first->second->getFilename() < second->second->getFilename();
                case 1:
                default:
                    return first->second->getCreationTime() < second->second->getCreationTime();
            }
        };
        std::stable_sort(hash_handles.begin(), hash_handles.end(), compare);
        layoutChanged();
    }

    void ImagesListModel::update_image(const Image &updater) {
        auto to_be_updated = image_map.find(updater.getSha1Sum());
        if(to_be_updated == image_map.end())
            return;

        *to_be_updated->second = updater;
        layoutChanged();
    }

    void ImagesListModel::setSettings(const Settings &settings) {
        ImagesListModel::settings = settings;
    }


    QJsonObject ImagesListModel::to_json() const {
        QJsonObject root{};
        QJsonArray image_array{};
        for (const auto & [k, image_ptr] : image_map) {
            image_array.push_back(image_ptr->to_json());
        }
        root[SETTINGS_KEY] = settings.to_json();
        root[IMAGE_LIST_KEY] =  image_array;
        return root;
    }

    ImagesListModel ImagesListModel::from_json(const QJsonObject & json) {
        std::vector<Image> images;
        std::map<std::string, std::unique_ptr<Image>> image_map;
        for (auto const & image_json : json[IMAGE_LIST_KEY].toArray()) {
            Image image = Image::from_json(image_json.toObject());
            image_map.insert({image.getSha1Sum(), make_unique<Image>(image)});
        }
        return ImagesListModel{
            Settings::from_json(json[SETTINGS_KEY].toObject()),
            std::move(image_map)
        };
    }


    ImagesListModel::ImagesListModel(Settings settings, std::map<std::string, std::unique_ptr<Image>> images,
                                     QObject *parent) : image_map(std::move(images)), settings(std::move(settings)){
        for (const auto & [key, ignore] : image_map) {
            hash_handles.emplace_back(key);
        }
    }

    bool ImagesListModel::save(const path &path) const {
        QFile file {path};
        if (!file.open(QIODevice::ReadWrite | QIODevice::Text | QIODeviceBase::Truncate))
            return false;
        file.write(QJsonDocument{this->to_json()}.toJson());
        file.close();
        return true;
    }

    void ImagesListModel::load(const path &path) {
        QFile file {path};
        file.open(QIODevice::ReadOnly | QIODevice::Text);
        ImagesListModel retval = ImagesListModel::from_json(QJsonDocument::fromJson(file.readAll()).object());
        file.close();
        this->image_map = std::move(retval.image_map);
        this->hash_handles = std::move(retval.hash_handles);
        this->settings = retval.settings;
        layoutChanged();
    }

    const Settings &ImagesListModel::getSettings() const {
        return settings;
    }

}
