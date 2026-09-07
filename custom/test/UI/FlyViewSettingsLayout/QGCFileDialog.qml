import QtQuick

QtObject {
    property var nameFilters
    property string title
    property string folder
    signal acceptedForLoad(string file)
    function openForLoad() {}
}
